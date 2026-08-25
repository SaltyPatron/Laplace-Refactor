#!/usr/bin/env python3
"""Build a content and reachability manifest for recoverable Git work."""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Sequence


SCHEMA = "laplace.git-recovery/v1"


class GitError(RuntimeError):
    pass


def git(repo: Path, arguments: Sequence[str], input_bytes: bytes | None = None) -> bytes:
    result = subprocess.run(
        ("git", "-C", str(repo), *arguments),
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", "replace").strip()
        raise GitError(f"git {' '.join(arguments)} failed ({result.returncode}): {detail}")
    return result.stdout


def text(repo: Path, arguments: Sequence[str]) -> str:
    return git(repo, arguments).decode("utf-8", "replace")


def classify_path(path: str) -> list[str]:
    lower = path.lower()
    domains: set[str] = set()
    if lower.endswith(".sql") or lower.startswith(("db/", "sql/")):
        domains.add("sql")
    if lower.startswith(("engine/", "extension/", "cmake/")) or lower.endswith(
        (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".cmake")
    ):
        domains.add("native")
    if lower.startswith("app/") or lower.endswith((".cs", ".csproj", ".sln", ".slnx")):
        domains.add("managed")
    if lower.startswith("web/") or lower.endswith((".ts", ".tsx", ".js", ".jsx", ".css")):
        domains.add("web")
    if "test" in lower or lower.startswith("tests/"):
        domains.add("tests")
    if lower.startswith(("deploy/", "scripts/")) or lower in ("justfile", "makefile"):
        domains.add("delivery")
    if lower.startswith("docs/") or lower.endswith((".md", ".rst")):
        domains.add("documentation")
    if lower.startswith("external/"):
        domains.add("dependencies")
    if not domains:
        domains.add("other")
    return sorted(domains)


def parse_commit_metadata(repo: Path, commits: list[str]) -> dict[str, dict[str, Any]]:
    if not commits:
        return {}
    output = git(
        repo,
        (
            "show",
            "-s",
            "--no-show-signature",
            "--format=%H%x00%P%x00%aI%x00%an%x00%ae%x00%cI%x00%cn%x00%ce%x00%s%x1e",
            *commits,
        ),
    ).decode("utf-8", "replace")
    records: dict[str, dict[str, Any]] = {}
    for raw_record in output.split("\x1e"):
        raw_record = raw_record.strip("\n")
        if not raw_record:
            continue
        fields = raw_record.split("\x00")
        if len(fields) != 9:
            raise GitError(f"unexpected commit metadata field count: {len(fields)}")
        commit, parents, author_time, author_name, author_email, commit_time, committer_name, committer_email, subject = fields
        records[commit] = {
            "commit": commit,
            "parents": parents.split() if parents else [],
            "author_time": author_time,
            "author_name": author_name,
            "author_email": author_email,
            "commit_time": commit_time,
            "committer_name": committer_name,
            "committer_email": committer_email,
            "subject": subject,
        }
    return records


def inspect_commit(repo: Path, commit: str) -> tuple[str, dict[str, Any]]:
    lineage = text(repo, ("rev-list", "--parents", "-n", "1", commit)).split()
    if len(lineage) > 1:
        numstat_arguments = ("diff", "-M", "-C", "--numstat", lineage[1], commit)
        name_arguments = ("diff", "-M", "-C", "--name-status", lineage[1], commit)
    else:
        numstat_arguments = (
            "diff-tree", "--root", "--no-commit-id", "-r", "-M", "-C", "--numstat", commit
        )
        name_arguments = (
            "diff-tree", "--root", "--no-commit-id", "-r", "-M", "-C", "--name-status", commit
        )
    numstat_output = text(repo, numstat_arguments)
    name_output = text(repo, name_arguments)
    insertions = 0
    deletions = 0
    binary_files = 0
    for line in numstat_output.splitlines():
        fields = line.split("\t", 2)
        if len(fields) < 3:
            continue
        if fields[0] == "-" or fields[1] == "-":
            binary_files += 1
        else:
            insertions += int(fields[0])
            deletions += int(fields[1])

    changes: list[dict[str, Any]] = []
    domains: set[str] = set()
    for line in name_output.splitlines():
        fields = line.split("\t")
        if len(fields) < 2:
            continue
        status = fields[0]
        if status.startswith(("R", "C")) and len(fields) >= 3:
            paths = [fields[1], fields[2]]
            change = {"status": status, "old_path": fields[1], "path": fields[2]}
        else:
            paths = [fields[1]]
            change = {"status": status, "path": fields[1]}
        change_domains = sorted({domain for path in paths for domain in classify_path(path)})
        change["domains"] = change_domains
        domains.update(change_domains)
        changes.append(change)
    return commit, {
        "files_changed": len(changes),
        "insertions": insertions,
        "deletions": deletions,
        "binary_files": binary_files,
        "domains": sorted(domains),
        "changes": changes,
    }


def patch_ids(repo: Path, revision_arguments: Sequence[str]) -> dict[str, str]:
    log_process = subprocess.Popen(
        (
            "git",
            "-C",
            str(repo),
            "log",
            "--no-merges",
            "--full-index",
            "--binary",
            "--pretty=format:commit %H",
            "-p",
            *revision_arguments,
        ),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert log_process.stdout is not None
    patch_process = subprocess.run(
        ("git", "patch-id", "--stable"),
        stdin=log_process.stdout,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    log_process.stdout.close()
    log_stderr = log_process.stderr.read() if log_process.stderr is not None else b""
    log_status = log_process.wait()
    if log_status != 0:
        raise GitError(f"git log for patch identities failed: {log_stderr.decode('utf-8', 'replace')}")
    if patch_process.returncode != 0:
        raise GitError(
            f"git patch-id failed: {patch_process.stderr.decode('utf-8', 'replace')}"
        )
    result: dict[str, str] = {}
    for line in patch_process.stdout.decode("ascii", "replace").splitlines():
        fields = line.split()
        if len(fields) == 2:
            result[fields[1]] = fields[0]
    return result


def inspect_refs(repo: Path, baseline: str) -> list[dict[str, Any]]:
    raw = git(
        repo,
        (
            "for-each-ref",
            "--format=%(refname)%00%(objectname)%00%(objecttype)%00%(upstream)%00%(upstream:trackshort)%1e",
            "refs/heads",
            "refs/remotes",
            "refs/stash",
        ),
    ).decode("utf-8", "replace")

    def inspect(raw_record: str) -> dict[str, Any] | None:
        fields = raw_record.strip("\n").split("\x00")
        if len(fields) != 5 or fields[2] != "commit":
            return None
        refname, target, object_type, upstream, tracking = fields
        merged_result = subprocess.run(
            ("git", "-C", str(repo), "merge-base", "--is-ancestor", target, baseline),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        ahead = int(text(repo, ("rev-list", "--count", f"{baseline}..{target}")).strip())
        behind = int(text(repo, ("rev-list", "--count", f"{target}..{baseline}")).strip())
        return {
            "ref": refname,
            "target": target,
            "object_type": object_type,
            "upstream": upstream,
            "tracking": tracking,
            "merged_into_baseline": merged_result.returncode == 0,
            "ahead_of_baseline": ahead,
            "behind_baseline": behind,
        }

    records = [item for item in raw.split("\x1e") if item.strip("\n")]
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        inspected = list(executor.map(inspect, records))
    return sorted((item for item in inspected if item is not None), key=lambda item: item["ref"])


def search_commits(repo: Path, pattern: str, baseline: str) -> list[str]:
    output = text(
        repo,
        (
            "log",
            "--all",
            "--not",
            baseline,
            "--format=%H",
            "-G",
            pattern,
        ),
    )
    return sorted(set(output.split()))


def deleted_blob(repo: Path, commit: str, parents: list[str], change: dict[str, Any]) -> dict[str, Any] | None:
    if not parents or not change["status"].startswith("D"):
        return None
    path = change["path"]
    result = subprocess.run(
        ("git", "-C", str(repo), "ls-tree", "-z", parents[0], "--", path),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0 or not result.stdout:
        return None
    header, _, returned_path = result.stdout.partition(b"\t")
    fields = header.decode("ascii", "replace").split()
    if len(fields) != 3:
        return None
    mode, object_type, object_id = fields
    size = int(text(repo, ("cat-file", "-s", object_id)).strip())
    return {
        "deleted_by": commit,
        "path": returned_path.rstrip(b"\x00").decode("utf-8", "surrogateescape"),
        "mode": mode,
        "object_type": object_type,
        "object_id": object_id,
        "size_bytes": size,
    }


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Inventory recoverable Git objects and changes.")
    parser.add_argument("--repo", required=True)
    parser.add_argument("--baseline", default="origin/main")
    parser.add_argument("--output", required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_arguments(argv)
    repo = Path(arguments.repo).resolve()
    output = Path(arguments.output).resolve()
    baseline = text(repo, ("rev-parse", "--verify", f"{arguments.baseline}^{{commit}}")).strip()
    commits = text(repo, ("rev-list", "--topo-order", "--reverse", "--all", "--not", baseline)).split()
    metadata = parse_commit_metadata(repo, commits)

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        change_records = dict(executor.map(lambda commit: inspect_commit(repo, commit), commits))
    baseline_patch_ids = patch_ids(repo, (baseline,))
    recovery_patch_ids = patch_ids(repo, ("--all", "--not", baseline))
    baseline_patch_to_commits: dict[str, list[str]] = collections.defaultdict(list)
    for commit, patch_id in baseline_patch_ids.items():
        baseline_patch_to_commits[patch_id].append(commit)
    recovery_patch_to_commits: dict[str, list[str]] = collections.defaultdict(list)
    for commit, patch_id in recovery_patch_ids.items():
        recovery_patch_to_commits[patch_id].append(commit)

    commit_records: list[dict[str, Any]] = []
    domain_totals: dict[str, dict[str, int]] = collections.defaultdict(
        lambda: {"commit_count": 0, "files_changed": 0, "insertions": 0, "deletions": 0}
    )
    deleted_records: list[dict[str, Any]] = []
    for commit in commits:
        record = {**metadata[commit], **change_records[commit]}
        record["is_merge"] = len(record["parents"]) > 1
        patch_id = recovery_patch_ids.get(commit, "")
        record["patch_id"] = patch_id
        record["baseline_equivalent_commits"] = sorted(
            baseline_patch_to_commits.get(patch_id, []) if patch_id else []
        )
        record["recovery_patch_equivalents"] = sorted(
            item for item in recovery_patch_to_commits.get(patch_id, []) if item != commit
        ) if patch_id else []
        for domain in record["domains"]:
            totals = domain_totals[domain]
            totals["commit_count"] += 1
            totals["files_changed"] += record["files_changed"]
            totals["insertions"] += record["insertions"]
            totals["deletions"] += record["deletions"]
        for change in record["changes"]:
            deletion = deleted_blob(repo, commit, record["parents"], change)
            if deletion is not None:
                deleted_records.append(deletion)
        commit_records.append(record)

    children_output = text(repo, ("rev-list", "--children", "--all", "--not", baseline))
    commit_set = set(commits)
    children: dict[str, list[str]] = {}
    for line in children_output.splitlines():
        fields = line.split()
        children[fields[0]] = [item for item in fields[1:] if item in commit_set]
    tips = sorted(commit for commit in commits if not children.get(commit))

    refs = inspect_refs(repo, baseline)
    refs_by_target: dict[str, list[str]] = collections.defaultdict(list)
    for item in refs:
        refs_by_target[item["target"]].append(item["ref"])
    tip_records = [
        {"commit": commit, "refs": sorted(refs_by_target.get(commit, []))} for commit in tips
    ]

    sql_commits = [record["commit"] for record in commit_records if "sql" in record["domains"]]
    campaign_patterns = {
        "parsed_atomic_bodies": r"BEGIN[[:space:]]+ATOMIC",
        "session_search_path": r"SET[[:space:]]+search_path",
        "function_configuration": r"proconfig",
        "spi_plan_preparation": r"SPI_prepare",
        "index_creation": r"CREATE[[:space:]]+(UNIQUE[[:space:]]+)?INDEX",
        "rowcount_loop": r"@@ROWCOUNT",
    }
    campaigns = {
        name: search_commits(repo, pattern, baseline)
        for name, pattern in campaign_patterns.items()
    }
    known_sql_commit = "3999680f307b4a893da86364557766c0872228a9"
    containing_refs = text(
        repo,
        ("for-each-ref", "--format=%(refname)", "--contains", known_sql_commit),
    ).split()

    status_lines = text(repo, ("status", "--short", "--untracked-files=all")).splitlines()
    stash_records = text(
        repo,
        ("stash", "list", "--format=%gd%x00%H%x00%aI%x00%s"),
    ).splitlines()
    stashes = []
    for line in stash_records:
        fields = line.split("\x00")
        if len(fields) == 4:
            stashes.append(
                {"selector": fields[0], "commit": fields[1], "time": fields[2], "subject": fields[3]}
            )
    fsck_output = text(repo, ("fsck", "--full", "--no-reflogs", "--unreachable"))
    unreachable_counts: dict[str, int] = collections.Counter()
    unreachable_objects: list[dict[str, str]] = []
    for line in fsck_output.splitlines():
        match = re.match(r"unreachable (\S+) ([0-9a-f]+)$", line)
        if match:
            object_type, object_id = match.groups()
            unreachable_counts[object_type] += 1
            unreachable_objects.append({"type": object_type, "object_id": object_id})

    report = {
        "schema": SCHEMA,
        "repository": str(repo),
        "baseline_ref": arguments.baseline,
        "baseline_commit": baseline,
        "summary": {
            "recoverable_commit_count": len(commit_records),
            "recoverable_nonmerge_commit_count": sum(not item["is_merge"] for item in commit_records),
            "recoverable_merge_commit_count": sum(item["is_merge"] for item in commit_records),
            "baseline_equivalent_patch_count": sum(
                bool(item["baseline_equivalent_commits"]) for item in commit_records
            ),
            "unique_recovery_patch_count": len(recovery_patch_to_commits),
            "duplicate_recovery_patch_group_count": sum(
                len(items) > 1 for items in recovery_patch_to_commits.values()
            ),
            "sql_commit_count": len(sql_commits),
            "deleted_blob_event_count": len(deleted_records),
            "ref_count": len(refs),
            "unmerged_ref_count": sum(not item["merged_into_baseline"] for item in refs),
            "tip_count": len(tip_records),
            "dirty_status_entry_count": len(status_lines),
            "stash_count": len(stashes),
            "unreachable_object_count": len(unreachable_objects),
        },
        "domain_totals": {name: values for name, values in sorted(domain_totals.items())},
        "known_sql_conversion": {
            "commit": known_sql_commit,
            "present_in_recovery_set": known_sql_commit in commit_set,
            "containing_refs": sorted(containing_refs),
        },
        "campaigns": campaigns,
        "tips": tip_records,
        "refs": refs,
        "commits": commit_records,
        "deleted_blobs": deleted_records,
        "dirty_status": status_lines,
        "stashes": stashes,
        "unreachable_counts": dict(sorted(unreachable_counts.items())),
        "unreachable_objects": sorted(
            unreachable_objects, key=lambda item: (item["type"], item["object_id"])
        ),
    }
    serialized = json.dumps(report, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f".{output.name}.tmp.{os.getpid()}")
    try:
        temporary.write_text(serialized, encoding="utf-8")
        os.replace(temporary, output)
    finally:
        if temporary.exists():
            temporary.unlink()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except GitError as error:
        print(f"inventory-git: {error}", file=sys.stderr)
        raise SystemExit(1) from error
