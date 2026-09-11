#!/usr/bin/env python3
"""Collapse exact merged-PR heads from a live branch-estate audit.

Patch equivalence cannot prove delivery for every squash-merged PR. GitHub's PR
record can: when a live branch tip exactly equals the head SHA of a PR whose
merged_at field is non-null, that branch was an input to an accepted merge.

This tool is deliberately narrower than semantic reconciliation:
- only exact head SHA matches are mechanically absorbed;
- closed-but-unmerged PRs are never absorbed;
- branches without a matching merged PR remain unchanged;
- the remaining patch/group counts are recomputed from unresolved branch records.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
from pathlib import Path
import sys
import time
from typing import Any
import urllib.error
import urllib.parse
import urllib.request


API_ROOT = "https://api.github.com"


def fetch_json(url: str, token: str | None) -> Any:
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "laplace-final-convergence-audit",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers)
    for attempt in range(4):
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            if exc.code in {502, 503, 504} and attempt < 3:
                time.sleep(2 ** attempt)
                continue
            detail = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"GitHub API {exc.code} for {url}: {detail}") from exc
        except urllib.error.URLError as exc:
            if attempt < 3:
                time.sleep(2 ** attempt)
                continue
            raise RuntimeError(f"GitHub API failure for {url}: {exc}") from exc
    raise AssertionError("unreachable")


def merged_pr_heads(full_name: str, token: str | None) -> dict[str, list[dict[str, Any]]]:
    by_head: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    page = 1
    while True:
        query = urllib.parse.urlencode(
            {
                "state": "closed",
                "sort": "updated",
                "direction": "desc",
                "per_page": 100,
                "page": page,
            }
        )
        payload = fetch_json(f"{API_ROOT}/repos/{full_name}/pulls?{query}", token)
        if not isinstance(payload, list):
            raise RuntimeError(f"unexpected PR listing payload for {full_name}")
        for pr in payload:
            merged_at = pr.get("merged_at")
            head = pr.get("head") or {}
            sha = head.get("sha")
            if not merged_at or not isinstance(sha, str) or len(sha) != 40:
                continue
            by_head[sha].append(
                {
                    "number": int(pr["number"]),
                    "title": str(pr.get("title") or ""),
                    "merged_at": str(merged_at),
                    "merge_commit_sha": pr.get("merge_commit_sha"),
                    "head_ref": head.get("ref"),
                }
            )
        print(
            f"{full_name}: scanned closed PR page {page}, rows={len(payload)}, "
            f"merged-heads={len(by_head)}",
            file=sys.stderr,
        )
        if len(payload) < 100:
            break
        page += 1
    for records in by_head.values():
        records.sort(key=lambda item: (item["number"], item["merged_at"]))
    return dict(by_head)


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
    unresolved = {
        "UNIQUE_BEHAVIOR_CANDIDATE",
        "PATCH_EQUIVALENT_BUT_TREE_DIVERGED",
        "NO_UNIQUE_COMMITS_BUT_TREE_DIVERGED",
    }
    repo["candidate_branch_count"] = sum(
        1 for branch in branches if branch["classification"] in unresolved
    )
    repo["merged_pr_head_count"] = sum(
        1 for branch in branches if branch["classification"] == "MERGED_PR_HEAD"
    )


def reconcile(
    report: dict[str, Any],
    repository_map: dict[str, str],
    token: str | None,
) -> dict[str, Any]:
    expected_schema = report.get("schema")
    if expected_schema not in {
        "laplace.branch-estate-live-audit/v2",
        "laplace.branch-estate-live-audit/v3",
    }:
        raise RuntimeError(f"unsupported input audit schema: {expected_schema!r}")

    proof_by_label: dict[str, dict[str, list[dict[str, Any]]]] = {}
    for label, full_name in repository_map.items():
        proof_by_label[label] = merged_pr_heads(full_name, token)

    for repo in report["repositories"]:
        label = repo["repository"]
        if label not in proof_by_label:
            raise RuntimeError(f"missing repository mapping for {label}")
        heads = proof_by_label[label]
        for branch in repo["branches"]:
            proofs = heads.get(branch["tip"])
            if not proofs:
                continue
            if branch["classification"] == "MERGED_PR_HEAD":
                branch["merged_prs"] = proofs
                continue
            branch["pre_merged_pr_classification"] = branch["classification"]
            branch["pre_merged_pr_missing_patch_count"] = branch.get("missing_patch_count", 0)
            branch["classification"] = "MERGED_PR_HEAD"
            branch["merged_prs"] = proofs
            branch["missing_patch_count"] = 0
            branch["missing_patch_keys"] = []
            branch["missing_patches"] = []
            branch["changed_files"] = []
        recompute(repo)

    report["schema"] = "laplace.branch-estate-live-audit/v4"
    report["merged_pr_head_rule"] = (
        "A live branch tip is mechanically delivered only when its exact 40-hex tip SHA "
        "equals the head SHA recorded by GitHub for a PR with non-null merged_at. Closed "
        "unmerged PRs remain unresolved. This proof supplements ancestry/content/patch "
        "equivalence and does not infer delivery from branch names."
    )
    return report


def print_summary(report: dict[str, Any]) -> None:
    print("BRANCH_ESTATE_LIVE_AUDIT_V4")
    for repo in report["repositories"]:
        print(f"\n{repo['repository']}")
        print(f"  branches including main: {repo['branch_count_including_main']}")
        print(f"  candidate branches: {repo['candidate_branch_count']}")
        print(f"  exact merged PR heads: {repo.get('merged_pr_head_count', 0)}")
        print(f"  unique missing patches: {repo['unique_missing_patch_count']}")
        print(
            "  missing-patch signature groups: "
            f"{repo['missing_patch_signature_group_count']}"
        )
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
        help="LABEL=OWNER/REPOSITORY; repeat for every repository in the ledger",
    )
    args = parser.parse_args()

    repository_map: dict[str, str] = {}
    for spec in args.repository:
        if "=" not in spec:
            parser.error(f"repository mapping must be LABEL=OWNER/REPO: {spec}")
        label, full_name = spec.split("=", 1)
        if not label or "/" not in full_name:
            parser.error(f"invalid repository mapping: {spec}")
        repository_map[label] = full_name

    report = json.loads(Path(args.ledger).read_text(encoding="utf-8"))
    token = os.environ.get("GITHUB_TOKEN") or None
    reconciled = reconcile(report, repository_map, token)
    Path(args.output).write_text(
        json.dumps(reconciled, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print_summary(reconciled)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
