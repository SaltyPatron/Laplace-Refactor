#!/usr/bin/env python3
"""Inventory an ELF dependency closure without loading target objects."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import platform
import re
import stat
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence


SCHEMA = "laplace.elf-closure/v1"
TOOL_VERSION = "1.0.0"
ELF_MAGIC = b"\x7fELF"


class InspectionError(RuntimeError):
    pass


def run_checked(argv: Sequence[str]) -> str:
    result = subprocess.run(
        argv,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise InspectionError(f"command failed ({result.returncode}): {argv!r}: {detail}")
    return result.stdout


def sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def is_within(path: str, prefix: str) -> bool:
    try:
        return os.path.commonpath((os.path.realpath(path), os.path.realpath(prefix))) == os.path.realpath(prefix)
    except ValueError:
        return False


def classify_path(path: str, custom_prefix: str) -> str:
    real = os.path.realpath(path)
    if is_within(real, custom_prefix):
        return "custom-prefix"
    if any(is_within(real, prefix) for prefix in ("/lib", "/usr/lib")):
        return "host-system"
    return "external-prefix"


def read_magic(path: str) -> bytes:
    try:
        with open(path, "rb") as stream:
            return stream.read(4)
    except (OSError, PermissionError):
        return b""


def split_colon_values(values: Iterable[str]) -> list[str]:
    result: list[str] = []
    for value in values:
        result.extend(value.split(":"))
    return result


def parse_bracket_value(line: str) -> str:
    match = re.search(r"\[([^]]*)\]\s*$", line)
    if match is None:
        raise InspectionError(f"cannot parse dynamic value: {line}")
    return match.group(1)


@dataclass(frozen=True)
class LoaderModel:
    path: str
    sha256: str
    lib_token: str
    platform_token: str
    system_directories: tuple[str, ...]
    diagnostics_sha256: str


def locate_loader() -> str:
    candidates = (
        "/lib64/ld-linux-x86-64.so.2",
        "/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2",
        "/lib/ld-linux-aarch64.so.1",
        "/lib64/ld-linux-aarch64.so.1",
    )
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate
    raise InspectionError("cannot locate the host ELF dynamic loader")


def inspect_loader(loader_path: str) -> LoaderModel:
    diagnostics = run_checked((loader_path, "--list-diagnostics"))
    lib_match = re.search(r'^dl_dst_lib="([^"]+)"$', diagnostics, re.MULTILINE)
    platform_match = re.search(r'^dl_platform="([^"]*)"$', diagnostics, re.MULTILINE)
    directory_matches = re.findall(
        r'^path\.system_dirs\[[^]]+\]="([^"]+)"$', diagnostics, re.MULTILINE
    )
    if lib_match is None or platform_match is None or not directory_matches:
        raise InspectionError("dynamic loader diagnostics did not expose token and search-path data")
    canonical_diagnostics = json.dumps(
        {
            "lib_token": lib_match.group(1),
            "platform_token": platform_match.group(1),
            "system_directories": [os.path.normpath(item) for item in directory_matches],
        },
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return LoaderModel(
        path=os.path.realpath(loader_path),
        sha256=sha256_file(loader_path),
        lib_token=lib_match.group(1),
        platform_token=platform_match.group(1),
        system_directories=tuple(os.path.normpath(item) for item in directory_matches),
        diagnostics_sha256=hashlib.sha256(canonical_diagnostics).hexdigest(),
    )


def inspect_ldconfig_cache() -> tuple[dict[str, list[str]], str, int]:
    cache_path = "/etc/ld.so.cache"
    output = run_checked(("/usr/sbin/ldconfig", "-p"))
    entries: dict[str, list[str]] = collections.defaultdict(list)
    for line in output.splitlines():
        match = re.match(r"^\s*(\S+)\s+\([^)]*\)\s+=>\s+(.+)$", line)
        if match is None:
            continue
        soname, path = match.groups()
        if path not in entries[soname]:
            entries[soname].append(path)
    cache_sha = sha256_file(cache_path) if os.path.isfile(cache_path) else ""
    return dict(entries), cache_sha, sum(len(paths) for paths in entries.values())


class ElfInspector:
    def __init__(self, readelf: str, custom_prefix: str) -> None:
        self.readelf = readelf
        self.custom_prefix = custom_prefix
        self._objects: dict[str, dict[str, Any]] = {}
        self._errors: dict[str, str] = {}

    @property
    def objects(self) -> dict[str, dict[str, Any]]:
        return self._objects

    @property
    def errors(self) -> dict[str, str]:
        return self._errors

    def inspect(self, path: str) -> dict[str, Any]:
        real = os.path.realpath(path)
        if real in self._objects:
            return self._objects[real]
        if real in self._errors:
            raise InspectionError(self._errors[real])
        try:
            result = self._inspect_uncached(real)
        except (InspectionError, OSError) as error:
            self._errors[real] = str(error)
            raise InspectionError(str(error)) from error
        self._objects[real] = result
        return result

    def _inspect_uncached(self, real: str) -> dict[str, Any]:
        if read_magic(real) != ELF_MAGIC:
            raise InspectionError(f"not an ELF object: {real}")
        header = run_checked((self.readelf, "--wide", "--file-header", real))
        dynamic = run_checked((self.readelf, "--wide", "--dynamic", real))
        program = run_checked((self.readelf, "--wide", "--program-headers", real))
        notes = run_checked((self.readelf, "--wide", "--notes", real))

        header_values: dict[str, str] = {}
        for field in ("Class", "Data", "Type", "Machine"):
            match = re.search(rf"^\s*{re.escape(field)}:\s*(.+?)\s*$", header, re.MULTILINE)
            if match is None:
                raise InspectionError(f"ELF header lacks {field}: {real}")
            header_values[field.lower()] = match.group(1)

        needed: list[str] = []
        sonames: list[str] = []
        rpaths: list[str] = []
        runpaths: list[str] = []
        for line in dynamic.splitlines():
            if "(NEEDED)" in line:
                needed.append(parse_bracket_value(line))
            elif "(SONAME)" in line:
                sonames.append(parse_bracket_value(line))
            elif "(RPATH)" in line:
                rpaths.append(parse_bracket_value(line))
            elif "(RUNPATH)" in line:
                runpaths.append(parse_bracket_value(line))

        interpreter_match = re.search(r"\[Requesting program interpreter:\s*([^]]+)\]", program)
        build_ids = sorted(set(re.findall(r"Build ID:\s*([0-9a-fA-F]+)", notes)))
        file_stat = os.stat(real)
        return {
            "path": real,
            "sha256": sha256_file(real),
            "size_bytes": file_stat.st_size,
            "mode": stat.filemode(file_stat.st_mode),
            "classification": classify_path(real, self.custom_prefix),
            "elf": {
                "class": header_values["class"],
                "data": header_values["data"],
                "type": header_values["type"],
                "machine": header_values["machine"],
                "interpreter": interpreter_match.group(1) if interpreter_match else "",
                "soname": sonames[0] if sonames else "",
                "needed": needed,
                "rpath": split_colon_values(rpaths),
                "runpath": split_colon_values(runpaths),
                "build_ids": build_ids,
            },
        }


def compatible(requester: dict[str, Any], candidate: dict[str, Any]) -> bool:
    request_header = requester["elf"]
    candidate_header = candidate["elf"]
    return all(
        request_header[field] == candidate_header[field]
        for field in ("class", "data", "machine")
    )


def expand_tokens(raw: str, origin: str, loader: LoaderModel, process_cwd: str) -> dict[str, Any]:
    expanded = raw
    replacements = {
        "${ORIGIN}": origin,
        "$ORIGIN": origin,
        "${LIB}": loader.lib_token,
        "$LIB": loader.lib_token,
        "${PLATFORM}": loader.platform_token,
        "$PLATFORM": loader.platform_token,
    }
    for token, value in replacements.items():
        expanded = expanded.replace(token, value)
    unknown_token = "$" in expanded
    empty_entry = expanded == ""
    relative_entry = not empty_entry and not os.path.isabs(expanded)
    if empty_entry:
        normalized = process_cwd
    elif relative_entry:
        normalized = os.path.normpath(os.path.join(process_cwd, expanded))
    else:
        normalized = os.path.normpath(expanded)
    return {
        "raw": raw,
        "origin": origin,
        "directory": normalized,
        "unknown_token": unknown_token,
        "empty_entry": empty_entry,
        "relative_entry": relative_entry,
    }


def directory_risks(entry: dict[str, Any]) -> list[str]:
    risks: list[str] = []
    if entry["unknown_token"]:
        risks.append("unexpanded-token")
    if entry["empty_entry"]:
        risks.append("working-directory-entry")
    if entry["relative_entry"]:
        risks.append("relative-entry")
    directory = entry["directory"]
    try:
        directory_stat = os.stat(directory)
        if not stat.S_ISDIR(directory_stat.st_mode):
            risks.append("not-a-directory")
        if directory_stat.st_mode & stat.S_IWOTH:
            risks.append("world-writable-directory")
    except OSError:
        risks.append("missing-directory")
    return risks


def discover_root_elfs(roots: Sequence[str]) -> tuple[list[dict[str, Any]], list[dict[str, str]]]:
    artifacts: list[dict[str, Any]] = []
    errors: list[dict[str, str]] = []
    seen: set[str] = set()

    def consider(path: str, root: str) -> None:
        absolute = os.path.abspath(path)
        if absolute in seen or read_magic(absolute) != ELF_MAGIC:
            return
        seen.add(absolute)
        artifacts.append(
            {
                "root": os.path.abspath(root),
                "lookup_path": absolute,
                "realpath": os.path.realpath(absolute),
            }
        )

    for root in roots:
        absolute_root = os.path.abspath(root)
        if os.path.isfile(absolute_root) or os.path.islink(absolute_root):
            consider(absolute_root, absolute_root)
            continue
        if not os.path.isdir(absolute_root):
            errors.append({"root": absolute_root, "error": "root-does-not-exist"})
            continue

        def onerror(error: OSError) -> None:
            errors.append({"root": absolute_root, "error": str(error)})

        for directory, directory_names, file_names in os.walk(
            absolute_root, followlinks=False, onerror=onerror
        ):
            directory_names.sort()
            file_names.sort()
            for name in file_names:
                consider(os.path.join(directory, name), absolute_root)
    artifacts.sort(key=lambda item: (item["lookup_path"], item["root"]))
    errors.sort(key=lambda item: (item["root"], item["error"]))
    return artifacts, errors


class ClosureBuilder:
    def __init__(
        self,
        inspector: ElfInspector,
        loader: LoaderModel,
        cache: dict[str, list[str]],
        custom_prefix: str,
        process_cwd: str,
        explicit_directories: Sequence[str],
    ) -> None:
        self.inspector = inspector
        self.loader = loader
        self.cache = cache
        self.custom_prefix = custom_prefix
        self.process_cwd = process_cwd
        self.explicit_directories = tuple(os.path.abspath(item) for item in explicit_directories)
        self.edges: dict[tuple[str, str, tuple[str, ...]], dict[str, Any]] = {}
        self.path_risks: dict[tuple[str, str, str], dict[str, Any]] = {}

    def child_inherited_rpaths(
        self,
        requester: dict[str, Any],
        inherited_rpaths: tuple[str, ...],
    ) -> tuple[str, ...]:
        elf = requester["elf"]
        if elf["runpath"]:
            return inherited_rpaths
        origin = os.path.dirname(requester["path"])
        own_rpaths = tuple(
            self.expanded_directories(
                elf["rpath"], origin, "dt-rpath", requester["path"]
            )
        )
        return (*own_rpaths, *inherited_rpaths)

    def expanded_directories(
        self,
        raw_entries: Sequence[str],
        origin: str,
        source: str,
        requester: str,
    ) -> list[str]:
        directories: list[str] = []
        for raw in raw_entries:
            entry = expand_tokens(raw, origin, self.loader, self.process_cwd)
            entry["source"] = source
            entry["requester"] = requester
            risks = directory_risks(entry)
            if risks:
                risk_record = dict(entry)
                risk_record["risks"] = risks
                self.path_risks[(source, requester, raw)] = risk_record
            directories.append(entry["directory"])
        return directories

    def add_directory_candidates(
        self,
        candidates: list[tuple[str, str]],
        rule: str,
        directories: Iterable[str],
        needed: str,
    ) -> None:
        for directory in directories:
            candidates.append((rule, os.path.normpath(os.path.join(directory, needed))))

    def resolve(
        self,
        requester: dict[str, Any],
        needed: str,
        inherited_rpaths: tuple[str, ...],
    ) -> dict[str, Any]:
        request_path = requester["path"]
        origin = os.path.dirname(request_path)
        elf = requester["elf"]
        raw_candidates: list[tuple[str, str]] = []

        if "/" in needed:
            direct_path = needed if os.path.isabs(needed) else os.path.join(self.process_cwd, needed)
            raw_candidates.append(("needed-path", os.path.normpath(direct_path)))
        else:
            own_rpaths: list[str] = []
            if not elf["runpath"]:
                own_rpaths = self.expanded_directories(
                    elf["rpath"], origin, "dt-rpath", request_path
                )
            self.add_directory_candidates(
                raw_candidates, "dt-rpath-chain", (*own_rpaths, *inherited_rpaths), needed
            )
            self.add_directory_candidates(
                raw_candidates, "explicit-library-path", self.explicit_directories, needed
            )
            runpaths = self.expanded_directories(
                elf["runpath"], origin, "dt-runpath", request_path
            )
            self.add_directory_candidates(raw_candidates, "dt-runpath", runpaths, needed)
            for cache_path in self.cache.get(needed, []):
                raw_candidates.append(("ldconfig-cache", os.path.normpath(cache_path)))
            self.add_directory_candidates(
                raw_candidates, "loader-system-directory", self.loader.system_directories, needed
            )

        candidate_records: list[dict[str, Any]] = []
        seen_lookup_paths: set[str] = set()
        selected: dict[str, Any] | None = None
        for precedence, (rule, lookup_path) in enumerate(raw_candidates):
            if lookup_path in seen_lookup_paths or not os.path.exists(lookup_path):
                continue
            seen_lookup_paths.add(lookup_path)
            record: dict[str, Any] = {
                "precedence": precedence,
                "rule": rule,
                "lookup_path": lookup_path,
                "realpath": os.path.realpath(lookup_path),
                "classification": classify_path(lookup_path, self.custom_prefix),
                "selected": False,
            }
            try:
                candidate_object = self.inspector.inspect(lookup_path)
                record["sha256"] = candidate_object["sha256"]
                record["compatible"] = compatible(requester, candidate_object)
            except InspectionError as error:
                record["sha256"] = ""
                record["compatible"] = False
                record["inspection_error"] = str(error)
            if selected is None and record["compatible"]:
                record["selected"] = True
                selected = record
            candidate_records.append(record)

        distinct_competitors = sorted(
            {
                (record["realpath"], record["sha256"])
                for record in candidate_records
                if record.get("compatible")
            }
        )
        status = "resolved" if selected is not None else "unresolved"
        return {
            "requester": request_path,
            "needed": needed,
            "inherited_rpaths": list(inherited_rpaths),
            "status": status,
            "selected_lookup_path": selected["lookup_path"] if selected else "",
            "selected_realpath": selected["realpath"] if selected else "",
            "selected_sha256": selected["sha256"] if selected else "",
            "selected_rule": selected["rule"] if selected else "",
            "competing_candidate_count": len(distinct_competitors),
            "candidates": candidate_records,
        }

    def build(self, roots: Sequence[dict[str, Any]]) -> None:
        queue: collections.deque[tuple[str, tuple[str, ...]]] = collections.deque()
        for artifact in roots:
            queue.append((artifact["realpath"], ()))
        visited: set[tuple[str, tuple[str, ...]]] = set()

        while queue:
            object_path, inherited_rpaths = queue.popleft()
            context_key = (object_path, inherited_rpaths)
            if context_key in visited:
                continue
            visited.add(context_key)
            try:
                requester = self.inspector.inspect(object_path)
            except InspectionError:
                continue
            elf = requester["elf"]
            child_inherited = self.child_inherited_rpaths(requester, inherited_rpaths)
            for needed in elf["needed"]:
                edge_key = (requester["path"], needed, inherited_rpaths)
                edge = self.resolve(requester, needed, inherited_rpaths)
                self.edges[edge_key] = edge
                if edge["status"] == "resolved":
                    queue.append((edge["selected_realpath"], child_inherited))

    def root_closure(self, artifact: dict[str, Any]) -> dict[str, Any]:
        root_path = artifact["realpath"]
        queue: collections.deque[tuple[str, tuple[str, ...]]] = collections.deque(
            ((root_path, ()),)
        )
        visited_contexts: set[tuple[str, tuple[str, ...]]] = set()
        object_paths: set[str] = set()
        chains: dict[str, list[str]] = {root_path: [root_path]}
        while queue:
            object_path, inherited_rpaths = queue.popleft()
            context = (object_path, inherited_rpaths)
            if context in visited_contexts:
                continue
            visited_contexts.add(context)
            object_paths.add(object_path)
            requester = self.inspector.objects.get(object_path)
            if requester is None:
                continue
            child_inherited = self.child_inherited_rpaths(requester, inherited_rpaths)
            for needed in requester["elf"]["needed"]:
                edge = self.edges.get((object_path, needed, inherited_rpaths))
                if edge is None or edge["status"] != "resolved":
                    continue
                child = edge["selected_realpath"]
                if child not in chains:
                    chains[child] = [*chains[object_path], child]
                queue.append((child, child_inherited))

        families: dict[str, list[dict[str, str]]] = collections.defaultdict(list)
        for path in sorted(object_paths):
            item = self.inspector.objects.get(path)
            if item is None:
                continue
            soname = item["elf"]["soname"]
            match = re.match(r"^(.*\.so)(?:\..*)?$", soname)
            if not soname or match is None:
                continue
            families[match.group(1)].append(
                {
                    "path": path,
                    "soname": soname,
                    "sha256": item["sha256"],
                    "chain": chains[path],
                }
            )
        collisions: list[dict[str, Any]] = []
        for family, members in sorted(families.items()):
            distinct = {(item["path"], item["sha256"]) for item in members}
            if len(distinct) < 2:
                continue
            sonames = {item["soname"] for item in members}
            collision_kind = (
                "distinct-soname-generations"
                if len(sonames) > 1
                else "same-soname-multiple-binaries"
            )
            collisions.append(
                {"family": family, "kind": collision_kind, "members": members}
            )
        return {
            "root_lookup_path": artifact["lookup_path"],
            "root_realpath": root_path,
            "object_count": len(object_paths),
            "abi_family_collisions": collisions,
        }


def summarize(
    root_artifacts: Sequence[dict[str, Any]],
    objects: Sequence[dict[str, Any]],
    edges: Sequence[dict[str, Any]],
    parse_errors: Sequence[dict[str, str]],
    candidate_inspection_errors: Sequence[dict[str, str]],
    discovery_errors: Sequence[dict[str, str]],
    custom_prefix: str,
) -> dict[str, int]:
    custom_objects = [item for item in objects if item["classification"] == "custom-prefix"]
    host_objects = [item for item in objects if item["classification"] == "host-system"]
    unresolved = [item for item in edges if item["status"] != "resolved"]
    competing = [item for item in edges if item["competing_candidate_count"] > 1]
    host_edges = [
        item
        for item in edges
        if item["status"] == "resolved"
        and classify_path(item["selected_realpath"], custom_prefix) == "host-system"
    ]
    object_classification = {item["path"]: item["classification"] for item in objects}
    custom_boundary_edges = [
        item
        for item in edges
        if item["status"] == "resolved"
        and object_classification.get(item["requester"]) == "custom-prefix"
        and object_classification.get(item["selected_realpath"]) != "custom-prefix"
    ]
    custom_host_edges = [
        item
        for item in custom_boundary_edges
        if object_classification.get(item["selected_realpath"]) == "host-system"
    ]
    custom_external_edges = [
        item
        for item in custom_boundary_edges
        if object_classification.get(item["selected_realpath"]) == "external-prefix"
    ]
    return {
        "root_artifact_count": len(root_artifacts),
        "object_count": len(objects),
        "custom_object_count": len(custom_objects),
        "host_object_count": len(host_objects),
        "edge_count": len(edges),
        "host_selected_edge_count": len(host_edges),
        "custom_boundary_edge_count": len(custom_boundary_edges),
        "custom_to_host_edge_count": len(custom_host_edges),
        "custom_to_external_edge_count": len(custom_external_edges),
        "unresolved_edge_count": len(unresolved),
        "competing_edge_count": len(competing),
        "parse_error_count": len(parse_errors),
        "candidate_inspection_error_count": len(candidate_inspection_errors),
        "discovery_error_count": len(discovery_errors),
    }


def build_resolution_conflicts(edges: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    selections: dict[str, set[tuple[str, str]]] = collections.defaultdict(set)
    for edge in edges:
        if edge["status"] == "resolved":
            selections[edge["needed"]].add(
                (edge["selected_realpath"], edge["selected_sha256"])
            )
    conflicts: list[dict[str, Any]] = []
    for needed, values in sorted(selections.items()):
        if len(values) < 2:
            continue
        conflicts.append(
            {
                "needed": needed,
                "selections": [
                    {"realpath": path, "sha256": digest}
                    for path, digest in sorted(values)
                ],
            }
        )
    return conflicts


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Produce an exact ELF dependency and loader-resolution inventory."
    )
    parser.add_argument("--root", action="append", required=True, help="ELF file or directory tree")
    parser.add_argument("--output", required=True, help="JSON report path or - for standard output")
    parser.add_argument("--custom-prefix", default="/opt/laplace", help="product-owned install prefix")
    parser.add_argument(
        "--search-dir", action="append", default=[], help="explicit library search directory"
    )
    parser.add_argument("--process-cwd", default="/", help="working directory used for relative paths")
    parser.add_argument("--readelf", default="/usr/bin/readelf", help="readelf executable")
    parser.add_argument("--loader", default="", help="host dynamic loader")
    parser.add_argument("--pretty", action="store_true", help="indent JSON output")
    parser.add_argument(
        "--strict", action="store_true", help="return failure when resolution or inspection is incomplete"
    )
    parser.add_argument(
        "--require-custom-closure",
        action="store_true",
        help="return failure when a selected dependency is outside the product prefix",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_arguments(argv)
    loader = inspect_loader(arguments.loader or locate_loader())
    cache, cache_sha, cache_entry_count = inspect_ldconfig_cache()
    inspector = ElfInspector(arguments.readelf, os.path.abspath(arguments.custom_prefix))
    root_artifacts, discovery_errors = discover_root_elfs(arguments.root)
    builder = ClosureBuilder(
        inspector=inspector,
        loader=loader,
        cache=cache,
        custom_prefix=os.path.abspath(arguments.custom_prefix),
        process_cwd=os.path.abspath(arguments.process_cwd),
        explicit_directories=arguments.search_dir,
    )
    builder.build(root_artifacts)

    root_realpaths = collections.defaultdict(list)
    for artifact in root_artifacts:
        root_realpaths[artifact["realpath"]].append(artifact["lookup_path"])
        try:
            root_object = inspector.inspect(artifact["realpath"])
            artifact["sha256"] = root_object["sha256"]
        except InspectionError as error:
            artifact["sha256"] = ""
            artifact["inspection_error"] = str(error)

    edges = sorted(
        builder.edges.values(),
        key=lambda item: (item["requester"], item["needed"], item["inherited_rpaths"]),
    )
    closure_paths = {artifact["realpath"] for artifact in root_artifacts}
    closure_paths.update(
        edge["selected_realpath"] for edge in edges if edge["status"] == "resolved"
    )
    objects: list[dict[str, Any]] = []
    for path, item in sorted(inspector.objects.items()):
        if path not in closure_paths:
            continue
        record = dict(item)
        record["root_lookup_paths"] = sorted(root_realpaths.get(path, []))
        objects.append(record)
    parse_errors = [
        {"path": path, "error": error}
        for path, error in sorted(inspector.errors.items())
        if path in closure_paths
    ]
    candidate_inspection_errors = [
        {"path": path, "error": error}
        for path, error in sorted(inspector.errors.items())
        if path not in closure_paths
    ]
    summary = summarize(
        root_artifacts,
        objects,
        edges,
        parse_errors,
        candidate_inspection_errors,
        discovery_errors,
        os.path.abspath(arguments.custom_prefix),
    )
    root_closures = [builder.root_closure(artifact) for artifact in root_artifacts]
    summary["roots_with_abi_family_collisions"] = sum(
        1 for item in root_closures if item["abi_family_collisions"]
    )
    summary["root_abi_family_collision_count"] = sum(
        len(item["abi_family_collisions"]) for item in root_closures
    )
    conflicts = build_resolution_conflicts(edges)
    summary["resolution_conflict_count"] = len(conflicts)
    report = {
        "schema": SCHEMA,
        "tool_version": TOOL_VERSION,
        "inputs": {
            "roots": sorted(os.path.abspath(item) for item in arguments.root),
            "custom_prefix": os.path.abspath(arguments.custom_prefix),
            "process_cwd": os.path.abspath(arguments.process_cwd),
            "explicit_search_directories": sorted(
                os.path.abspath(item) for item in arguments.search_dir
            ),
            "environment_library_path_used": False,
        },
        "host_loader": {
            "machine": platform.machine(),
            "path": loader.path,
            "sha256": loader.sha256,
            "lib_token": loader.lib_token,
            "platform_token": loader.platform_token,
            "system_directories": list(loader.system_directories),
            "diagnostics_sha256": loader.diagnostics_sha256,
            "ldconfig_cache_path": "/etc/ld.so.cache",
            "ldconfig_cache_sha256": cache_sha,
            "ldconfig_entry_count": cache_entry_count,
        },
        "resolution_model": {
            "target_objects_loaded": False,
            "metadata_reader": arguments.readelf,
            "precedence": [
                "needed-path",
                "dt-rpath-chain when dt-runpath is absent",
                "explicit-library-path",
                "dt-runpath",
                "ldconfig-cache",
                "loader-system-directory",
            ],
        },
        "summary": summary,
        "root_artifacts": root_artifacts,
        "root_closures": root_closures,
        "objects": objects,
        "edges": edges,
        "resolution_conflicts": conflicts,
        "search_path_risks": sorted(
            builder.path_risks.values(),
            key=lambda item: (
                item["source"],
                item["requester"],
                item["origin"],
                item["directory"],
                item["raw"],
            ),
        ),
        "parse_errors": parse_errors,
        "candidate_inspection_errors": candidate_inspection_errors,
        "discovery_errors": discovery_errors,
    }

    serialized = json.dumps(
        report,
        indent=2 if arguments.pretty else None,
        sort_keys=True,
        separators=None if arguments.pretty else (",", ":"),
    ) + "\n"
    if arguments.output == "-":
        sys.stdout.write(serialized)
    else:
        output_path = Path(arguments.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        temporary_path = output_path.with_name(f".{output_path.name}.tmp.{os.getpid()}")
        try:
            temporary_path.write_text(serialized, encoding="utf-8")
            os.replace(temporary_path, output_path)
        finally:
            if temporary_path.exists():
                temporary_path.unlink()

    incomplete = any(
        summary[field] > 0
        for field in ("unresolved_edge_count", "parse_error_count", "discovery_error_count")
    )
    outside_custom = any(
        edge["status"] == "resolved"
        and not is_within(edge["selected_realpath"], arguments.custom_prefix)
        for edge in edges
    )
    if arguments.strict and incomplete:
        return 1
    if arguments.require_custom_closure and outside_custom:
        return 2
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except InspectionError as error:
        print(f"elf-closure: {error}", file=sys.stderr)
        raise SystemExit(70) from error
