#!/usr/bin/env python3
"""Reconcile live branch tips against exact GitHub pull-request delivery records.

Patch equivalence cannot prove delivery for every squash-merged PR. GitHub's PR
record can: when a live branch tip exactly equals the head SHA of a PR whose
merged_at field is non-null, that branch was an input to an accepted merge.

A second, deliberately narrow proof handles closed-unmerged PRs whose own body
explicitly records one replacement using one of these exact forms:
  "Superseded by #N", "Absorbed into #N", or "Replaced by #N".
That target is followed only through equally explicit same-repository chains and
the chain is accepted only if it terminates at a merged PR. Vague prose, branch
names, dates, and inferred similarity never establish supersession.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
from pathlib import Path
import re
import sys
import time
from typing import Any
import urllib.error
import urllib.parse
import urllib.request


API_ROOT = "https://api.github.com"
EXPLICIT_SUPERSESSION = re.compile(
    r"\b(?:superseded by|absorbed into|replaced by)\s+#(\d+)\b",
    re.IGNORECASE,
)


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


def pr_record(pr: dict[str, Any]) -> dict[str, Any] | None:
    head = pr.get("head") or {}
    sha = head.get("sha")
    if not isinstance(sha, str) or len(sha) != 40:
        return None
    return {
        "number": int(pr["number"]),
        "title": str(pr.get("title") or ""),
        "body": str(pr.get("body") or ""),
        "closed_at": pr.get("closed_at"),
        "merged_at": pr.get("merged_at"),
        "merge_commit_sha": pr.get("merge_commit_sha"),
        "head_ref": head.get("ref"),
        "head_sha": sha,
    }


def closed_pr_inventory(
    full_name: str,
    token: str | None,
) -> tuple[dict[str, list[dict[str, Any]]], dict[int, dict[str, Any]]]:
    by_head: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    by_number: dict[int, dict[str, Any]] = {}
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
            record = pr_record(pr)
            if record is None:
                continue
            by_head[record["head_sha"]].append(record)
            by_number[record["number"]] = record
        merged_heads = sum(
            1 for records in by_head.values() if any(r.get("merged_at") for r in records)
        )
        print(
            f"{full_name}: scanned closed PR page {page}, rows={len(payload)}, "
            f"merged-heads={merged_heads}, exact-heads={len(by_head)}",
            file=sys.stderr,
        )
        if len(payload) < 100:
            break
        page += 1
    for records in by_head.values():
        records.sort(key=lambda item: item["number"])
    return dict(by_head), by_number


def compact_pr(record: dict[str, Any]) -> dict[str, Any]:
    return {
        "number": record["number"],
        "title": record["title"],
        "closed_at": record.get("closed_at"),
        "merged_at": record.get("merged_at"),
        "merge_commit_sha": record.get("merge_commit_sha"),
        "head_ref": record.get("head_ref"),
        "head_sha": record.get("head_sha"),
    }


def explicit_target(record: dict[str, Any]) -> int | None:
    matches = {int(value) for value in EXPLICIT_SUPERSESSION.findall(record.get("body") or "")}
    if len(matches) != 1:
        return None
    return next(iter(matches))


def supersession_chain(
    source: dict[str, Any],
    by_number: dict[int, dict[str, Any]],
) -> list[dict[str, Any]] | None:
    if source.get("merged_at"):
        return None
    target_number = explicit_target(source)
    if target_number is None:
        return None

    chain = [compact_pr(source)]
    seen = {source["number"]}
    for _ in range(12):
        target = by_number.get(target_number)
        if target is None or target["number"] in seen:
            return None
        seen.add(target["number"])
        chain.append(compact_pr(target))
        if target.get("merged_at"):
            return chain
        target_number = explicit_target(target)
        if target_number is None:
            return None
    return None


def clear_missing(branch: dict[str, Any]) -> None:
    branch["missing_patch_count"] = 0
    branch["missing_patch_keys"] = []
    branch["missing_patches"] = []
    branch["changed_files"] = []


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
    repo["explicit_pr_supersession_count"] = sum(
        1 for branch in branches if branch["classification"] == "EXPLICIT_PR_SUPERSESSION"
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

    inventory_by_label: dict[
        str, tuple[dict[str, list[dict[str, Any]]], dict[int, dict[str, Any]]]
    ] = {}
    for label, full_name in repository_map.items():
        inventory_by_label[label] = closed_pr_inventory(full_name, token)

    for repo in report["repositories"]:
        label = repo["repository"]
        if label not in inventory_by_label:
            raise RuntimeError(f"missing repository mapping for {label}")
        by_head, by_number = inventory_by_label[label]
        for branch in repo["branches"]:
            exact_prs = by_head.get(branch["tip"], [])
            if not exact_prs:
                continue

            branch["exact_closed_prs"] = [compact_pr(record) for record in exact_prs]
            merged = [record for record in exact_prs if record.get("merged_at")]
            if merged:
                branch["pre_merged_pr_classification"] = branch["classification"]
                branch["pre_merged_pr_missing_patch_count"] = branch.get("missing_patch_count", 0)
                branch["classification"] = "MERGED_PR_HEAD"
                branch["merged_prs"] = [compact_pr(record) for record in merged]
                clear_missing(branch)
                continue

            chains = [
                chain
                for record in exact_prs
                if (chain := supersession_chain(record, by_number)) is not None
            ]
            if len(chains) == 1:
                branch["pre_pr_supersession_classification"] = branch["classification"]
                branch["pre_pr_supersession_missing_patch_count"] = branch.get(
                    "missing_patch_count", 0
                )
                branch["classification"] = "EXPLICIT_PR_SUPERSESSION"
                branch["supersession_chain"] = chains[0]
                clear_missing(branch)
        recompute(repo)

    report["schema"] = "laplace.branch-estate-live-audit/v4"
    report["pull_request_delivery_rule"] = (
        "A live branch tip is mechanically delivered when its exact 40-hex tip SHA equals "
        "the head SHA recorded by GitHub for a merged PR. A closed-unmerged exact head is "
        "also dispositioned only when its own PR body contains exactly one explicit "
        "'Superseded by #N', 'Absorbed into #N', or 'Replaced by #N' reference and that "
        "same-repository chain terminates at a merged PR. All other closed-unmerged PRs "
        "remain unresolved. No branch-name or similarity inference is used."
    )
    return report


def print_summary(report: dict[str, Any]) -> None:
    print("BRANCH_ESTATE_LIVE_AUDIT_V4")
    for repo in report["repositories"]:
        print(f"\n{repo['repository']}")
        print(f"  branches including main: {repo['branch_count_including_main']}")
        print(f"  candidate branches: {repo['candidate_branch_count']}")
        print(f"  exact merged PR heads: {repo.get('merged_pr_head_count', 0)}")
        print(f"  explicit PR supersessions: {repo.get('explicit_pr_supersession_count', 0)}")
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
