#!/usr/bin/env python3
"""Collapse unresolved branch candidates that are exact ancestors of other candidates.

This does not claim the descendant is delivered. It only removes duplicate semantic
review: if candidate A's tip is an exact Git ancestor of unresolved candidate B,
then B's review necessarily includes every commit reachable from A. A is therefore
subsumed by the maximal unresolved descendant lineage(s).

No branch name, date, PR status, or patch similarity is used for this proof.
"""

from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path
import subprocess
from typing import Any


CANDIDATE = "UNIQUE_BEHAVIOR_CANDIDATE"
SUBSUMED = "SUBSUMED_BY_CANDIDATE_BRANCH"


def git(repo: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(repo), *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def is_ancestor(repo: Path, ancestor: str, descendant: str) -> bool:
    return git(repo, "merge-base", "--is-ancestor", ancestor, descendant).returncode == 0


def recompute(repo: dict[str, Any]) -> None:
    branches = repo["branches"]
    by_missing_signature: dict[tuple[str, ...], list[str]] = collections.defaultdict(list)
    patch_to_branches: dict[str, set[str]] = collections.defaultdict(set)
    patch_details: dict[str, dict[str, Any]] = {}

    for branch in branches:
        keys = tuple(branch.get("missing_patch_keys") or [])
        if keys:
            by_missing_signature[keys].append(branch["branch"])
        for patch in branch.get("missing_patches") or []:
            key = patch["key"]
            patch_to_branches[key].add(branch["branch"])
            patch_details.setdefault(key, {k: v for k, v in patch.items() if k != "branches"})

    repo["missing_patch_signature_groups"] = [
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
    repo["missing_patches"] = [
        {
            **patch_details[key],
            "branches": sorted(patch_to_branches[key]),
            "branch_count": len(patch_to_branches[key]),
        }
        for key in sorted(patch_details)
    ]
    repo["missing_patch_signature_group_count"] = len(repo["missing_patch_signature_groups"])
    repo["unique_missing_patch_count"] = len(repo["missing_patches"])

    counts = collections.Counter(branch["classification"] for branch in branches)
    repo["classification_counts"] = dict(sorted(counts.items()))
    repo["candidate_branch_count"] = sum(
        1 for branch in branches if branch["classification"] == CANDIDATE
    )
    repo["subsumed_candidate_branch_count"] = sum(
        1 for branch in branches if branch["classification"] == SUBSUMED
    )


def reconcile_repo(repo_dir: Path, document: dict[str, Any]) -> None:
    candidates = [
        branch for branch in document["branches"] if branch["classification"] == CANDIDATE
    ]
    by_name = {branch["branch"]: branch for branch in candidates}

    descendants: dict[str, list[str]] = collections.defaultdict(list)
    for index, left in enumerate(candidates):
        for right in candidates[index + 1 :]:
            if left["tip"] == right["tip"]:
                continue
            if is_ancestor(repo_dir, left["tip"], right["tip"]):
                descendants[left["branch"]].append(right["branch"])
            elif is_ancestor(repo_dir, right["tip"], left["tip"]):
                descendants[right["branch"]].append(left["branch"])

    for branch_name, all_descendants in sorted(descendants.items()):
        branch = by_name[branch_name]
        # Retain only maximal unresolved descendants. If X is an ancestor of Y,
        # Y alone is sufficient to cover this source branch's reachable history.
        maximal: list[str] = []
        for candidate_name in all_descendants:
            candidate = by_name[candidate_name]
            has_later_descendant = False
            for other_name in all_descendants:
                if candidate_name == other_name:
                    continue
                other = by_name[other_name]
                if is_ancestor(repo_dir, candidate["tip"], other["tip"]):
                    has_later_descendant = True
                    break
            if not has_later_descendant:
                maximal.append(candidate_name)

        if not maximal:
            continue
        branch["pre_subsumption_classification"] = branch["classification"]
        branch["pre_subsumption_missing_patch_count"] = branch.get("missing_patch_count", 0)
        branch["classification"] = SUBSUMED
        branch["subsumed_by"] = [
            {
                "branch": name,
                "tip": by_name[name]["tip"],
            }
            for name in sorted(maximal)
        ]
        branch["missing_patch_count"] = 0
        branch["missing_patch_keys"] = []
        branch["missing_patches"] = []
        branch["changed_files"] = []

    recompute(document)


def print_summary(report: dict[str, Any]) -> None:
    print("BRANCH_ESTATE_LIVE_AUDIT_V5")
    for repo in report["repositories"]:
        print(f"\n{repo['repository']}")
        print(f"  branches including main: {repo['branch_count_including_main']}")
        print(f"  unresolved maximal candidate branches: {repo['candidate_branch_count']}")
        print(f"  candidate ancestor branches subsumed: {repo.get('subsumed_candidate_branch_count', 0)}")
        print(f"  unique missing patches: {repo['unique_missing_patch_count']}")
        print(f"  missing-patch signature groups: {repo['missing_patch_signature_group_count']}")
        for key, value in repo["classification_counts"].items():
            print(f"  {key}: {value}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ledger", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument(
        "--repository",
        action="append",
        required=True,
        help="LABEL=PATH; repeat for every repository in the ledger",
    )
    args = parser.parse_args()

    repo_map: dict[str, Path] = {}
    for spec in args.repository:
        if "=" not in spec:
            parser.error(f"repository mapping must be LABEL=PATH: {spec}")
        label, raw_path = spec.split("=", 1)
        repo_map[label] = Path(raw_path).resolve()

    report = json.loads(Path(args.ledger).read_text(encoding="utf-8"))
    if report.get("schema") != "laplace.branch-estate-live-audit/v4":
        raise RuntimeError(f"expected v4 merged-PR ledger, got {report.get('schema')!r}")

    for repository in report["repositories"]:
        label = repository["repository"]
        if label not in repo_map:
            raise RuntimeError(f"missing local repository path for {label}")
        reconcile_repo(repo_map[label], repository)

    report["schema"] = "laplace.branch-estate-live-audit/v5"
    report["candidate_subsumption_rule"] = (
        "An unresolved branch is removed from independent semantic review only when its exact "
        "tip commit is a Git ancestor of another unresolved candidate tip. The descendant "
        "remains unresolved; reviewing its maximal lineage covers the ancestor's reachable "
        "commits. No delivery claim is inferred from this relation."
    )
    Path(args.output).write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print_summary(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
