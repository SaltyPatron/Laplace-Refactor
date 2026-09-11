#!/usr/bin/env python3
"""Audit live remote branches against authoritative main.

This is a reconciliation inventory, not a deletion tool. It never mutates refs.
It proves mechanically absorbed refs and commits using ancestry, tree identity,
stable patch equivalence, and exact post-commit path state. Everything else is
grouped by the non-merge patch set still requiring behavior-level reconciliation.
"""

from __future__ import annotations

import argparse
import collections
import json
import pathlib
import subprocess
import sys
from dataclasses import asdict, dataclass


def git(repo: pathlib.Path, *args: str, check: bool = True) -> str:
    proc = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(
            f"git {' '.join(args)} failed in {repo}: {proc.stderr.strip()}"
        )
    return proc.stdout


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
    return sorted(
        ref
        for ref in (line.strip() for line in raw.splitlines())
        if ref.startswith("origin/") and ref not in {"origin/HEAD", "origin/main"}
    )


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


def commit_parents(repo: pathlib.Path, commit: str) -> list[str]:
    return git(repo, "show", "-s", "--format=%P", commit).strip().split()


def is_merge(repo: pathlib.Path, commit: str) -> bool:
    return len(commit_parents(repo, commit)) > 1


def commit_subject(repo: pathlib.Path, commit: str) -> str:
    return git(repo, "show", "-s", "--format=%s", commit).strip()


def commit_changed_files(repo: pathlib.Path, commit: str) -> list[str]:
    parents = commit_parents(repo, commit)
    if parents:
        raw = git(
            repo,
            "diff-tree",
            "--no-commit-id",
            "--name-only",
            "-r",
            parents[0],
            commit,
        )
    else:
        raw = git(
            repo,
            "diff-tree",
            "--root",
            "--no-commit-id",
            "--name-only",
            "-r",
            commit,
        )
    return [line.strip() for line in raw.splitlines() if line.strip()]


def path_state(repo: pathlib.Path, ref: str, path: str) -> str:
    """Return exact Git tree entry state (mode/type/object/name), or MISSING."""
    proc = run(repo, "ls-tree", ref, "--", path)
    if proc.returncode != 0:
        raise RuntimeError(
            f"git ls-tree {ref} -- {path} failed in {repo}: {proc.stderr.strip()}"
        )
    value = proc.stdout.rstrip("\n")
    return value if value else "MISSING"


def final_path_state_absorbed(
    repo: pathlib.Path,
    main: str,
    commit: str,
    files: tuple[str, ...],
    cache: dict[tuple[str, str], str],
) -> bool:
    if not files:
        return True
    for path in files:
        commit_key = (commit, path)
        main_key = (main, path)
        if commit_key not in cache:
            cache[commit_key] = path_state(repo, commit, path)
        if main_key not in cache:
            cache[main_key] = path_state(repo, main, path)
        if cache[commit_key] != cache[main_key]:
            return False
    return True


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
    lines = patch.stdout.strip().splitlines()
    if not lines:
        return None
    return lines[0].split()[0]


def main_patch_ids(repo: pathlib.Path, main: str, cache: dict[str, str | None]) -> set[str]:
    ids: set[str] = set()
    commits = [
        line.strip()
        for line in git(repo, "rev-list", "--no-merges", main).splitlines()
        if line.strip()
    ]
    for index, commit in enumerate(commits, 1):
        pid = cache.get(commit)
        if commit not in cache:
            pid = patch_id(repo, commit)
            cache[commit] = pid
        if pid:
            ids.add(pid)
        if index % 500 == 0:
            print(f"patch-id baseline: {index}/{len(commits)} commits", file=sys.stderr)
    return ids


def changed_files(repo: pathlib.Path, main: str, ref: str) -> list[str]:
    return [
        line.strip()
        for line in git(repo, "diff", "--name-only", main, ref, "--").splitlines()
        if line.strip()
    ]


@dataclass(frozen=True)
class MissingPatch:
    key: str
    patch_id: str | None
    commit: str
    subject: str
    changed_files: tuple[str, ...]


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
    final_state_absorbed_commits: int
    missing_patch_count: int
    missing_patch_keys: list[str]
    missing_patches: list[dict]
    classification: str
    changed_files: list[str]


def audit_repo(repo: pathlib.Path, label: str) -> dict:
    main = "origin/main"
    main_sha = rev(repo, main)
    main_tree = tree(repo, main)
    refs = list_remote_branches(repo)
    print(f"{label}: {len(refs) + 1} branch refs including main", file=sys.stderr)

    patch_cache: dict[str, str | None] = {}
    subject_cache: dict[str, str] = {}
    changed_cache: dict[str, tuple[str, ...]] = {}
    state_cache: dict[tuple[str, str], str] = {}
    baseline_patch_ids = main_patch_ids(repo, main, patch_cache)
    records: list[BranchRecord] = []
    patch_to_branches: dict[str, set[str]] = collections.defaultdict(set)
    patch_details: dict[str, MissingPatch] = {}
    final_state_absorbed_commits: set[str] = set()

    for index, ref in enumerate(refs, 1):
        name = ref.removeprefix("origin/")
        tip_sha = rev(repo, ref)
        tree_sha = tree(repo, ref)
        ahead, behind = ahead_behind(repo, main, ref)
        uniques = unique_commits(repo, main, ref)
        nonmerges = [commit for commit in uniques if not is_merge(repo, commit)]
        missing_records: list[MissingPatch] = []
        branch_final_state_absorbed = 0

        for commit in nonmerges:
            if commit not in changed_cache:
                changed_cache[commit] = tuple(commit_changed_files(repo, commit))
            commit_files = changed_cache[commit]
            if not commit_files:
                # Empty metadata/checkpoint commits are not missing behavior.
                continue
            if commit not in patch_cache:
                patch_cache[commit] = patch_id(repo, commit)
            pid = patch_cache[commit]
            if pid is not None and pid in baseline_patch_ids:
                continue
            if final_path_state_absorbed(
                repo, main, commit, commit_files, state_cache
            ):
                final_state_absorbed_commits.add(commit)
                branch_final_state_absorbed += 1
                continue
            if commit not in subject_cache:
                subject_cache[commit] = commit_subject(repo, commit)
            # A patch-id failure with changed final state remains fail-closed and
            # is keyed by commit SHA rather than silently treated as absorbed.
            key = f"patch:{pid}" if pid else f"commit:{commit}"
            record = MissingPatch(
                key=key,
                patch_id=pid,
                commit=commit,
                subject=subject_cache[commit],
                changed_files=commit_files,
            )
            missing_records.append(record)
            patch_to_branches[key].add(name)
            patch_details.setdefault(key, record)

        unique_missing = {record.key: record for record in missing_records}
        ordered_missing = [unique_missing[key] for key in sorted(unique_missing)]

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
            elif nonmerges and not ordered_missing:
                classification = "MECHANICALLY_ABSORBED_BUT_TREE_DIVERGED"
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
                final_state_absorbed_commits=branch_final_state_absorbed,
                missing_patch_count=len(ordered_missing),
                missing_patch_keys=[record.key for record in ordered_missing],
                missing_patches=[asdict(record) for record in ordered_missing],
                classification=classification,
                changed_files=files,
            )
        )
        if index % 25 == 0 or index == len(refs):
            print(f"{label}: audited {index}/{len(refs)} non-main refs", file=sys.stderr)

    by_tip: dict[str, list[str]] = collections.defaultdict(list)
    by_tree: dict[str, list[str]] = collections.defaultdict(list)
    by_missing_signature: dict[tuple[str, ...], list[str]] = collections.defaultdict(list)
    for record in records:
        by_tip[record.tip].append(record.branch)
        by_tree[record.tree].append(record.branch)
        if record.missing_patch_keys:
            by_missing_signature[tuple(record.missing_patch_keys)].append(record.branch)

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
    missing_patch_groups = [
        {
            "missing_patch_keys": list(signature),
            "branches": sorted(names),
            "patch_count": len(signature),
        }
        for signature, names in sorted(
            by_missing_signature.items(),
            key=lambda item: (-len(item[1]), len(item[0]), item[1][0]),
        )
    ]
    missing_patches = [
        {
            **asdict(patch_details[key]),
            "branches": sorted(patch_to_branches[key]),
            "branch_count": len(patch_to_branches[key]),
        }
        for key in sorted(patch_details)
    ]

    counts = collections.Counter(record.classification for record in records)
    candidate_records = [
        record
        for record in records
        if record.classification
        in {
            "UNIQUE_BEHAVIOR_CANDIDATE",
            "NO_UNIQUE_COMMITS_BUT_TREE_DIVERGED",
        }
    ]

    return {
        "repository": label,
        "main": main_sha,
        "main_tree": main_tree,
        "branch_count_including_main": len(refs) + 1,
        "classification_counts": dict(sorted(counts.items())),
        "candidate_branch_count": len(candidate_records),
        "final_state_absorbed_commit_count": len(final_state_absorbed_commits),
        "unique_missing_patch_count": len(missing_patches),
        "missing_patch_signature_group_count": len(missing_patch_groups),
        "duplicate_tip_groups": duplicate_tip_groups,
        "duplicate_tree_groups": duplicate_tree_groups,
        "missing_patch_signature_groups": missing_patch_groups,
        "missing_patches": missing_patches,
        "branches": [asdict(record) for record in records],
    }


def print_summary(report: dict) -> None:
    print("BRANCH_ESTATE_LIVE_AUDIT")
    for repo in report["repositories"]:
        print(f"\n{repo['repository']}")
        print(f"  main: {repo['main']}")
        print(f"  branches including main: {repo['branch_count_including_main']}")
        print(f"  candidate branches: {repo['candidate_branch_count']}")
        print(
            "  exact final-state absorbed commits: "
            f"{repo['final_state_absorbed_commit_count']}"
        )
        print(f"  unique missing patches: {repo['unique_missing_patch_count']}")
        print(
            "  missing-patch signature groups: "
            f"{repo['missing_patch_signature_group_count']}"
        )
        for key, value in repo["classification_counts"].items():
            print(f"  {key}: {value}")
        print(f"  duplicate tip groups: {len(repo['duplicate_tip_groups'])}")
        print("  largest shared missing-patch groups:")
        for group in repo["missing_patch_signature_groups"][:10]:
            print(
                f"    branches={len(group['branches'])} patches={group['patch_count']} "
                f"example={group['branches'][0]}"
            )


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
        repositories.append(audit_repo(pathlib.Path(raw_path).resolve(), label))

    report = {
        "schema": "laplace.branch-estate-live-audit/v3",
        "reconciliation_rule": (
            "Exact ancestry/content, stable patch equivalence, and exact post-commit "
            "path-state equivalence are mechanically absorbed. Everything else "
            "requires behavior-level reconciliation before retirement; historical "
            "tree divergence alone is never counted as proof of missing behavior."
        ),
        "repositories": repositories,
    }
    output = pathlib.Path(args.output)
    output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print_summary(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
