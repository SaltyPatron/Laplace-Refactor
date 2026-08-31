#!/usr/bin/env python3
"""Static acceptance for GitHub's transitive skipped-job propagation boundary."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
WORKFLOW = REPOSITORY / ".github" / "workflows" / "product-path.yml"


class DevBatDeploymentReachabilityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.workflow = WORKFLOW.read_text(encoding="utf-8")

    def job_block(self, job_id: str, next_job_id: str | None = None) -> str:
        start = self.workflow.index(f"  {job_id}:\n")
        if next_job_id is None:
            return self.workflow[start:]
        end = self.workflow.index(f"  {next_job_id}:\n", start)
        return self.workflow[start:end]

    def test_dev_bat_dispatch_survives_skipped_optional_proof_jobs(self) -> None:
        block = self.job_block("dev-bat-deployment", "legacy-requirements")
        self.assertIn("    needs: product-path\n", block)
        self.assertIn("    if: >-\n      always() &&\n", block)
        self.assertIn("github.event_name == 'push'", block)
        self.assertIn("github.ref == 'refs/heads/main'", block)
        self.assertIn("needs.product-path.result == 'success'", block)
        self.assertIn("actions/workflows/product-activation.yml/dispatches", block)

    def test_legacy_protection_aliases_survive_skipped_optional_proof_jobs(self) -> None:
        aliases = (
            ("legacy-requirements", "legacy-native-dev"),
            ("legacy-native-dev", "legacy-native-sanitize"),
            ("legacy-native-sanitize", None),
        )
        for job_id, next_job_id in aliases:
            with self.subTest(job_id=job_id):
                block = self.job_block(job_id, next_job_id)
                self.assertIn("    needs: product-path\n", block)
                self.assertIn(
                    "    if: always() && needs.product-path.result == 'success'\n",
                    block,
                )

    def test_deliberate_missing_always_defect_is_rejected(self) -> None:
        mutant = self.workflow.replace(
            "    if: >-\n      always() &&\n      github.event_name == 'push'",
            "    if: >-\n      github.event_name == 'push'",
            1,
        )
        self.assertNotEqual(mutant, self.workflow)
        start = mutant.index("  dev-bat-deployment:\n")
        end = mutant.index("  legacy-requirements:\n", start)
        self.assertNotIn("always() &&", mutant[start:end])


if __name__ == "__main__":
    unittest.main()
