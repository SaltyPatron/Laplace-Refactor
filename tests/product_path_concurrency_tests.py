#!/usr/bin/env python3

from pathlib import Path
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
WORKFLOW_PATH = REPOSITORY / ".github/workflows/product-path.yml"
PHYSICAL_WORKFLOWS = (
    REPOSITORY / ".github/workflows/custom-stack.yml",
    REPOSITORY / ".github/workflows/postgresql-product.yml",
    REPOSITORY / ".github/workflows/package-product.yml",
)


class ProductPathConcurrencyTests(unittest.TestCase):
    def test_pull_request_runs_cancel_obsolete_active_head_without_cancelling_main(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        marker = (
            "concurrency:\n"
            "  group: product-path-${{ github.ref }}\n"
            "  queue: single\n"
            "  cancel-in-progress: ${{ github.event_name == 'pull_request' }}\n"
        )
        self.assertIn(
            marker,
            workflow,
            "new PR heads must replace superseded same-PR work while accepted-main runs remain non-cancelling",
        )
        self.assertNotIn(
            "  cancel-in-progress: true\n",
            workflow[workflow.index("concurrency:"):workflow.index("\njobs:")],
            "top-level product-path concurrency must never unconditionally cancel accepted-main proof",
        )

    def test_physical_host_ownership_queues_every_pending_proof(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        publication = workflow[
            workflow.index("  publication-recovery:") : workflow.index("  custom-stack-proof:")
        ]
        live_substrate = workflow[
            workflow.index("  dev-bat-live-substrate:") : workflow.index("  legacy-requirements:")
        ]
        for name, block in {
            "publication-recovery": publication,
            "dev-bat-live-substrate": live_substrate,
        }.items():
            self.assertIn("group: laplace-physical-product-proof", block, name)
            self.assertIn("queue: max", block, name)
            self.assertIn("cancel-in-progress: false", block, name)

    def test_reusable_physical_workflows_queue_instead_of_replace(self) -> None:
        marker = (
            "concurrency:\n"
            "  group: laplace-physical-product-proof\n"
            "  queue: max\n"
            "  cancel-in-progress: false\n"
        )
        for path in PHYSICAL_WORKFLOWS:
            with self.subTest(workflow=path.name):
                self.assertIn(marker, path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
