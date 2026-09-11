#!/usr/bin/env python3
"""Audit live remote branches against authoritative main without trusting stale ledgers.

This is a reconciliation inventory, not a deletion tool. It never mutates refs.
It classifies mechanically provable cases and leaves every other branch as a
candidate requiring behavior-level reconciliation before retirement.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
import subprocess
import sys
from dataclasses import dataclass, asdict
from typing import Iterable


def git(repo: pathlib.Path, *args: str, check: bool = True, text: bool = True) -> str:
    proc = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=text,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(
            f"git {' '.join(args)} failed in {repo}: {proc.stderr.strip()}"
        )
    return proc.stdout if text else proc.stdout.decode("utf-8", errors="replace")


def run(repo: pathlib.Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(repo), *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def list_remote_branches(repo: pathlib.Path) -> list[str]:
    raw = git(
        repo,
        "for-each-ref",
        "--format=%(refname:short)",
        "refs/remotes/origin",
    )
    refs: list[str] = []
    for line in raw.splitlines():
        ref = line.strip()
        if not ref or ref == "origin/HEAD" or ref == "origin/main":
            continue
        if not ref.startswith("origin/"):
            continue
        refs.append(ref)
    return sorted(refs)


def rev(repo: pathlib.Path, ref: str) -> str:
    return git(repo, "rev-parse", ref).strip()


def tree(repo: pathlib.Path, ref: str) -> str:
    return git(repo, "rev-parse", f"{ref}^{{tree}}").strip()


def is_ancestor(repo: pathlib.Path, ancestor: str, descendant: str) -> bool:
    return run(repo, "merge-base", "--is-ancestor", ancestor, descendant).returncode == 0


def ahead_behind(repo: pathlib.Path, main: str, ref: str) -> tuple[int, int]:
    raw = git(repo, "rev-list", "--left-right", "--count", f"{main}...{ref}").strip()
    left, right = raw.split()
    return int(right), int(left)


def unique_commits(repo: pathlib.Path, main: str, ref: str) -> list[str]:
    raw = git(repo, "rev-list", "--reverse", ref, f"^{main}")
    return [line.strip() for line in raw.splitlines() if line.strip()]


def is_merge(repo: pathlib.Path, commit: str) -> bool:
    parents = git(repo, "show", "-s", "--format=%P", commit).strip().split()
    return len(parents) > 1


def patch_id(repo: pathlib.Path, commit: str) -> str | None:
    show = subprocess.Popen(
        ["git", "-C", str(repo), "show", "--pretty=format:%H", "--binary", commit],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    patch = subprocess.run(
        ["git", "patch-id", "--stable"],
        stdin=show.stdout,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if show.stdout is not None:
        show.stdout.close()
    show.wait()
    if show.returncode != 0 or patch.returncode != 0:
        return None
    line = patch.stdout.strip().splitlines()
    if not line:
        return None
    return line[0].split()[0]


def main_patch_ids(repo: pathlib.Path, main: str) -> set[str]:
    # Compute once per repository. This deliberately excludes merge commits;
    # merge conflict resolutions are handled conservatively by tree/content checks.
    ids: set[str] = set()
    raw = git(repo, "rev-list", "--no-merges", main)
    commits = [line.strip() for line in raw.splitlines() if line.strip()]
    for idx, commit in enumerate(commits, 1):
        pid = patch_id(repo, commit)
        if pid:
            ids.add(pid)
        if idx % 500 == 0:
            print(f"patch-id baseline: {idx}/{len(commits)} commits", file=sys.stderr)
    return ids


def changed_files(repo: pathlib.Path, main: str, ref: str) -> list[str]:
    raw = git(repo, "diff", "--name-only", main, ref, "--")
    return [line.strip() for line in raw.splitlines() if line.strip()]


@dataclass
class BranchRecord:
    branch: str
    ref: str
    tip: str
    tree: str
    ahead: int
    behind: int
    unique_commits: int
    unique_nonmerge_commits: int
    missing_patch_ids: int
    classification: str
    changed_files: list[str]


def audit_repo(repo: pathlib.Path, label: str) -> dict:
    main = "origin/main"
    main_sha = rev(repo, main)
    main_tree = tree(repo, main)
    refs = list_remote_branches(repo)
    print(f"{label}: {len(refs) + 1} branch refs including main", file=sys.stderr)

    baseline_patch_ids = main_patch_ids(repo, main)
    records: list[BranchRecord] = []

    for index, ref in enumerate(refs, 1):
        name = ref.removeprefix("origin/")
        tip_sha = rev(repo, ref)
        tree_sha = tree(repo, ref)
        ahead, behind = ahead_behind(repo, main, ref)
        uniques = unique_commits(repo, main, ref)
        nonmerges = [commit for commit in uniques if not is_merge(repo, commit)]
        missing = 0
        for commit in nonmerges:
            pid = patch_id(repo, commit)
            if pid is None or pid not in baseline_patch_ids:
                missing += 1

        if tip_sha == main_sha:
            classification = "SAME_TIP_AS_MAIN"
            files: list[str] = []
        elif is_ancestor(repo, ref, main):
            classification = "ANCESTOR_OF_MAIN"
            files = []
        elif tree_sha == main_tree:
            classification = "SAME_TREE_AS_MAIN"
            files = []
        else:
            files = changed_files(repo, main, ref)
            if not files:
                classification = "SAME_CONTENT_AS_MAIN"
            elif nonmerges and missing == 0:
                classification = "PATCH_EQUIVALENT_BUT_TREE_DIVERGED"
            elif not uniques:
                classification = "NO_UNIQUE_COMMITS_BUT_TREE_DIVERGED"
            else:
                classification = "UNIQUE_BEHAVIOR_CANDIDATE"

        records.append(
            BranchRecord(
                branch=name,
                ref=ref,
                tip=tip_sha,
                tree=tree_sha,
                ahead=ahead,
                behind=behind,
                unique_commits=len(uniques),
                unique_nonmerge_commits=len(nonmerges),
                missing_patch_ids=missing,
                classification=classification,
                changed_files=files,
            )
        )
        if index % 25 == 0 or index == len(refs):
            print(f"{label}: audited {index}/{len(refs)} non-main refs", file=sys.stderr)

    by_tip: dict[str, list[str]] = collections.defaultdict(list)
    by_tree: dict[str, list[str]] = collections.defaultdict(list)
    for record in records:
        by_tip[record.tip].append(record.branch)
        by_tree[record.tree].append(record.branch)

    duplicate_tip_groups = [
        {"tip": sha, "branches": sorted(names)}
        for sha, names in sorted(by_tip.items())
        if len(names) > 1
    ]
    duplicate_tree_groups = [
        {"tree": sha, "branches": sorted(names)}
        for sha, names in sorted(by_tree.items())
        if len(names) > 1
    ]

    counts = collections.Counter(record.classification for record in records)
    candidate_records = [
        record for record in records
        if record.classification in {
            "UNIQUE_BEHAVIOR_CANDIDATE",
            "PATCH_EQUIVALENT_BUT_TREE_DIVERGED",
            "NO_UNIQUE_COMMITS_BUT_TREE_DIVERGED",
        }
    ]
    candidate_trees: dict[str, list[str]] = collections.defaultdict(list)
    for record in candidate_records:
        candidate_trees[record.tree].append(record.branch)

    return {
        "repository": label,
        "main": main_sha,
        "main_tree": main_tree,
        "branch_count_including_main": len(refs) + 1,
        "classification_counts": dict(sorted(counts.items())),
        "candidate_branch_count": len(candidate_records),
        "candidate_tree_group_count": len(candidate_trees),
        "duplicate_tip_groups": duplicate_tip_groups,
        "duplicate_tree_groups": duplicate_tree_groups,
        "candidate_tree_groups": [
            {"tree": sha, "branches": sorted(names)}
            for sha, names in sorted(candidate_trees.items())
        ],
        "branches": [asdict(record) for record in records],
    }


def print_summary(report: dict) -> None:
    print("BRANCH_ESTATE_LIVE_AUDIT")
    for repo in report["repositories"]:
        print(f"\n{repo['repository']}")
        print(f"  main: {repo['main']}")
        print(f"  branches including main: {repo['branch_count_including_main']}")
        print(f"  candidate branches: {repo['candidate_branch_count']}")
        print(f"  candidate tree groups: {repo['candidate_tree_group_count']}")
        for key, value in repo["classification_counts"].items():
            print(f"  {key}: {value}")
        print(f"  duplicate tip groups: {len(repo['duplicate_tip_groups'])}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "repo",
        nargs="+",
        help="repository specs as LABEL=PATH (for example Laplace-Refactor=.)",
    )
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    repositories = []
    for spec in args.repo:
        if "=" not in spec:
            parser.error(f"repository spec must be LABEL=PATH: {spec}")
        label, raw_path = spec.split("=", 1)
        path = pathlib.Path(raw_path).resolve()
        repositories.append(audit_repo(path, label))

    report = {
        "schema": "laplace.branch-estate-live-audit/v1",
        "reconciliation_rule": (
            "Only SAME_TIP/ANCESTOR/SAME_TREE/SAME_CONTENT are mechanically safe. "
            "All other branches require behavior-level reconciliation before retirement."
        ),
        "repositories": repositories,
    }
    output = pathlib.Path(args.output)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print_summary(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
