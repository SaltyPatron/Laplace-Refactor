#!/usr/bin/env python3
"""Apply reviewed exact-tip semantic dispositions to a v5 branch-estate ledger.

Disposition files are intentionally not wildcard ignore lists. Every entry names:
- repository label,
- exact branch name,
- exact 40-hex live tip,
- terminal semantic disposition,
- rationale and evidence.

If the live branch moved, the audit fails. If a later mechanical proof already absorbs
that exact unchanged branch, the manual disposition becomes redundant and is retained as
review metadata without overwriting the stronger mechanical classification. Everything
not explicitly dispositioned remains unresolved.
"""

from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path
import re
from typing import Any


HEX40 = re.compile(r"^[0-9a-f]{40}$")
CANDIDATE = "UNIQUE_BEHAVIOR_CANDIDATE"
TERMINAL = "SEMANTICALLY_DISPOSITIONED"


def load(path: str) -> dict[str, Any]:
    value = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError(f"{path} must contain one JSON object")
    return value


def validate_dispositions(document: dict[str, Any]) -> list[dict[str, Any]]:
    if document.get("schema") != "laplace.branch-estate-semantic-dispositions/v1":
        raise RuntimeError("unsupported semantic-disposition schema")
    entries = document.get("entries")
    if not isinstance(entries, list) or not entries:
        raise RuntimeError("semantic-disposition ledger must contain entries")
    seen: set[tuple[str, str]] = set()
    result: list[dict[str, Any]] = []
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise RuntimeError(f"disposition entry {index} is not an object")
        repository = entry.get("repository")
        branch = entry.get("branch")
        tip = entry.get("tip")
        disposition = entry.get("disposition")
        rationale = entry.get("rationale")
        evidence = entry.get("evidence")
        if not all(isinstance(value, str) and value.strip() for value in
                   (repository, branch, tip, disposition, rationale)):
            raise RuntimeError(f"disposition entry {index} has a missing string field")
        if HEX40.fullmatch(tip) is None:
            raise RuntimeError(f"disposition entry {index} has invalid tip {tip!r}")
        if not isinstance(evidence, list) or not evidence or not all(
            isinstance(value, str) and value.strip() for value in evidence
        ):
            raise RuntimeError(f"disposition entry {index} must contain evidence strings")
        key = (repository, branch)
        if key in seen:
            raise RuntimeError(f"duplicate semantic disposition for {repository}:{branch}")
        seen.add(key)
        result.append(entry)
    return result


def validate_global_uniqueness(entries: list[dict[str, Any]]) -> None:
    seen: set[tuple[str, str]] = set()
    for entry in entries:
        key = (entry["repository"], entry["branch"])
        if key in seen:
            raise RuntimeError(
                f"duplicate semantic disposition across ledgers for {key[0]}:{key[1]}"
            )
        seen.add(key)


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
    repo["candidate_branch_count"] = sum(
        1 for branch in branches if branch["classification"] == CANDIDATE
    )
    repo["semantic_disposition_count"] = sum(
        1 for branch in branches if branch["classification"] == TERMINAL
    )
    dispositions = collections.Counter(
        branch.get("semantic_disposition")
        for branch in branches
        if branch["classification"] == TERMINAL
    )
    repo["semantic_disposition_counts"] = dict(
        sorted((key, value) for key, value in dispositions.items() if key)
    )


def apply(
    report: dict[str, Any],
    entries: list[dict[str, Any]],
) -> dict[str, Any]:
    if report.get("schema") != "laplace.branch-estate-live-audit/v5":
        raise RuntimeError(f"expected v5 branch-estate ledger, got {report.get('schema')!r}")

    repositories = {repo["repository"]: repo for repo in report["repositories"]}
    branches_by_repo = {
        label: {branch["branch"]: branch for branch in repo["branches"]}
        for label, repo in repositories.items()
    }

    applied = 0
    redundant = 0
    for entry in entries:
        label = entry["repository"]
        name = entry["branch"]
        if label not in repositories:
            raise RuntimeError(f"semantic disposition names unknown repository {label!r}")
        branch = branches_by_repo[label].get(name)
        if branch is None:
            raise RuntimeError(f"semantic disposition names missing live branch {label}:{name}")
        if branch["tip"] != entry["tip"]:
            raise RuntimeError(
                f"semantic disposition is stale for {label}:{name}: "
                f"ledger tip={entry['tip']} live tip={branch['tip']}"
            )

        proof = {
            "disposition": entry["disposition"],
            "rationale": entry["rationale"],
            "evidence": list(entry["evidence"]),
            "exact_tip": entry["tip"],
        }
        if branch["classification"] != CANDIDATE:
            branch.setdefault("redundant_semantic_dispositions", []).append(proof)
            redundant += 1
            continue

        branch["pre_semantic_classification"] = branch["classification"]
        branch["pre_semantic_missing_patch_count"] = branch.get("missing_patch_count", 0)
        branch["classification"] = TERMINAL
        branch["semantic_disposition"] = entry["disposition"]
        branch["semantic_disposition_rationale"] = entry["rationale"]
        branch["semantic_disposition_evidence"] = list(entry["evidence"])
        branch["semantic_disposition_tip"] = entry["tip"]
        clear_missing(branch)
        applied += 1

    for repo in report["repositories"]:
        recompute(repo)

    report["schema"] = "laplace.branch-estate-live-audit/v6"
    report["semantic_disposition_rule"] = (
        "Manual semantic dispositions apply only to exact repository/branch/tip SHA entries "
        "from the supplied semantic-disposition ledgers. A moved tip fails the audit. A "
        "mechanically resolved unchanged branch keeps the stronger mechanical classification "
        "and records manual review as redundant. Every unlisted maximal candidate remains "
        "unresolved."
    )
    report["semantic_dispositions_applied"] = applied
    report["semantic_dispositions_redundant"] = redundant
    return report


def print_summary(report: dict[str, Any]) -> None:
    print("BRANCH_ESTATE_LIVE_AUDIT_V6")
    print(f"  semantic dispositions applied: {report['semantic_dispositions_applied']}")
    print(f"  semantic dispositions redundant: {report['semantic_dispositions_redundant']}")
    for repo in report["repositories"]:
        print(f"\n{repo['repository']}")
        print(f"  unresolved maximal candidate branches: {repo['candidate_branch_count']}")
        print(f"  semantic dispositions: {repo.get('semantic_disposition_count', 0)}")
        print(f"  unique missing patches: {repo['unique_missing_patch_count']}")
        print(f"  missing-patch signature groups: {repo['missing_patch_signature_group_count']}")
        for key, value in repo.get("semantic_disposition_counts", {}).items():
            print(f"  disposition {key}: {value}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ledger", required=True)
    parser.add_argument("--dispositions", action="append", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    report = load(args.ledger)
    entries: list[dict[str, Any]] = []
    for path in args.dispositions:
        entries.extend(validate_dispositions(load(path)))
    validate_global_uniqueness(entries)
    result = apply(report, entries)
    Path(args.output).write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print_summary(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
