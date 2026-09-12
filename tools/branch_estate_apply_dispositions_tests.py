#!/usr/bin/env python3
"""Regression tests for exact-tip semantic disposition lifecycle rules."""

from __future__ import annotations

import copy
import unittest

import branch_estate_apply_dispositions as subject


TIP = "1" * 40
MOVED_TIP = "2" * 40
RETIRED_TIP = "3" * 40


def report() -> dict:
    return {
        "schema": "laplace.branch-estate-live-audit/v5",
        "repositories": [
            {
                "repository": "Laplace-Refactor",
                "branches": [
                    {
                        "branch": "live-candidate",
                        "tip": TIP,
                        "classification": subject.CANDIDATE,
                        "missing_patch_count": 1,
                        "missing_patch_keys": ["patch:one"],
                        "missing_patches": [
                            {
                                "key": "patch:one",
                                "patch_id": "one",
                                "commit": TIP,
                                "subject": "candidate",
                                "changed_files": ["engine/example.cpp"],
                            }
                        ],
                        "changed_files": ["engine/example.cpp"],
                    }
                ],
                "missing_patch_signature_groups": [],
                "missing_patches": [],
                "missing_patch_signature_group_count": 0,
                "unique_missing_patch_count": 0,
            }
        ],
    }


def disposition(branch: str, tip: str) -> dict:
    return {
        "repository": "Laplace-Refactor",
        "branch": branch,
        "tip": tip,
        "disposition": "ABSORBED_BY_REVIEWED_CONVERGENCE",
        "rationale": "Exact-tip behavior was reviewed against convergence.",
        "evidence": ["proof"],
    }


class SemanticDispositionLifecycleTests(unittest.TestCase):
    def test_retired_branch_is_preserved_without_clearing_live_candidate(self) -> None:
        value = report()
        result = subject.apply(value, [disposition("retired-branch", RETIRED_TIP)])

        branch = result["repositories"][0]["branches"][0]
        self.assertEqual(branch["classification"], subject.CANDIDATE)
        self.assertEqual(branch["missing_patch_count"], 1)
        self.assertEqual(result["semantic_dispositions_applied"], 0)
        self.assertEqual(result["semantic_dispositions_retired_count"], 1)
        self.assertEqual(
            result["semantic_dispositions_retired"][0]["status"],
            "REVIEWED_BRANCH_NO_LONGER_LIVE",
        )
        self.assertEqual(
            result["semantic_dispositions_retired"][0]["exact_tip"], RETIRED_TIP
        )

    def test_exact_live_tip_can_classify_only_that_candidate(self) -> None:
        value = report()
        result = subject.apply(value, [disposition("live-candidate", TIP)])

        branch = result["repositories"][0]["branches"][0]
        self.assertEqual(branch["classification"], subject.TERMINAL)
        self.assertEqual(branch["missing_patch_count"], 0)
        self.assertEqual(result["semantic_dispositions_applied"], 1)
        self.assertEqual(result["semantic_dispositions_retired_count"], 0)

    def test_moved_live_tip_still_fails_closed(self) -> None:
        value = report()
        value["repositories"][0]["branches"][0]["tip"] = MOVED_TIP

        with self.assertRaisesRegex(RuntimeError, "semantic disposition is stale"):
            subject.apply(value, [disposition("live-candidate", TIP)])

    def test_unknown_repository_still_fails_closed(self) -> None:
        value = report()
        entry = disposition("retired-branch", RETIRED_TIP)
        entry["repository"] = "Unknown"

        with self.assertRaisesRegex(RuntimeError, "unknown repository"):
            subject.apply(copy.deepcopy(value), [entry])


if __name__ == "__main__":
    unittest.main(verbosity=2)
