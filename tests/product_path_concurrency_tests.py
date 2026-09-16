#!/usr/bin/env python3

from pathlib import Path
import re
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
WORKFLOW_PATH = REPOSITORY / ".github/workflows/product-path.yml"
PHYSICAL_WORKFLOWS = (
    REPOSITORY / ".github/workflows/custom-stack.yml",
    REPOSITORY / ".github/workflows/postgresql-product.yml",
    REPOSITORY / ".github/workflows/package-product.yml",
)
HEAVY_JOBS = (
    "custom-stack-proof",
    "candidate-chess-calibration",
    "postgresql-product-proof",
    "package-product-proof",
    "dev-bat-deployment",
    "deployed-chess-calibration",
    "deployed-stockfish-corpus",
    "dev-bat-live-substrate",
)


def job_condition(workflow: str, name: str) -> str:
    block = workflow.split(f"  {name}:\n", 1)[1]
    block = re.split(r"(?m)^  [A-Za-z0-9_-]+:\s*$", block, maxsplit=1)[0]
    match = re.search(r"(?m)^    if: (.+)(?:\n|$)", block)
    if match is None:
        raise AssertionError(f"{name} is missing its job condition")
    if match.group(1) == ">-":
        lines = []
        for line in block[match.end():].splitlines():
            if not line.startswith("      "):
                break
            lines.append(line.strip())
        return " ".join(lines)
    return match.group(1)


def condition_allows(expression: str, context: dict[str, str], cancelled: bool) -> bool:
    # Evaluate the actual checked-in job predicate for both scheduling and GitHub's
    # cancellation re-evaluation, with no builtins or ambient workflow state.
    actual = expression.replace("!cancelled()", repr(not cancelled))
    actual = actual.replace("always()", "True").replace("&&", "and").replace("||", "or")
    actual = re.sub(r"\b(?:needs|github)\.[A-Za-z0-9_.-]+", lambda m: repr(context[m.group()]), actual)
    return bool(eval(actual, {"__builtins__": {}}, {}))


class ProductPathConcurrencyTests(unittest.TestCase):
    @staticmethod
    def admitted_context(job: str) -> dict[str, str]:
        return {
            "github.event_name": "pull_request" if job == "candidate-chess-calibration" else "push",
            "github.ref": "refs/heads/main",
            "github.event.pull_request.head.repo.full_name": "owner/repo",
            "github.repository": "owner/repo",
            **{f"needs.{name}.result": "success" for name in (
                "hosted-proof", "publication-recovery", "custom-stack-proof",
                "postgresql-product-proof", "product-path", "dev-bat-deployment",
            )},
            **{f"needs.classify.outputs.requires_{name}": "true" for name in (
                "custom_stack", "postgresql_product", "package_product", "chess_calibration",
                "deployment", "stockfish_corpus",
            )},
        }

    def assert_heavy_jobs_cancel(self, workflow: str) -> None:
        for job in HEAVY_JOBS:
            condition = job_condition(workflow, job)
            context = self.admitted_context(job)
            self.assertTrue(condition_allows(condition, context, False), job)
            self.assertFalse(
                condition_allows(condition, context, True),
                f"{job} would start or continue after the run is cancelled",
            )

    def test_heavy_jobs_stop_when_the_run_is_cancelled(self) -> None:
        self.assert_heavy_jobs_cancel(WORKFLOW_PATH.read_text(encoding="utf-8"))

    def test_each_old_always_guard_defeats_cancellation_and_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        for job in HEAVY_JOBS:
            with self.subTest(job=job):
                condition = job_condition(workflow, job)
                mutant = condition.replace("(!cancelled())", "always()", 1)
                self.assertNotEqual(condition, mutant)
                self.assertTrue(condition_allows(mutant, self.admitted_context(job), True))
                prefix, suffix = workflow.split(f"  {job}:\n", 1)
                mutant_workflow = prefix + f"  {job}:\n" + suffix.replace("(!cancelled())", "always()", 1)
                with self.assertRaises(AssertionError):
                    self.assert_heavy_jobs_cancel(mutant_workflow)

    def test_cancellable_jobs_preserve_prerequisite_failures_and_optional_skips(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        for job in HEAVY_JOBS:
            condition = job_condition(workflow, job)
            context = self.admitted_context(job)
            for dependency in set(re.findall(r"needs\.[A-Za-z0-9_-]+\.result", condition)):
                for result in ("failure", "cancelled", "skipped", ""):
                    with self.subTest(job=job, dependency=dependency, result=result):
                        expected = job == "custom-stack-proof" and dependency == "needs.publication-recovery.result" and result == "skipped"
                        self.assertEqual(condition_allows(condition, context | {dependency: result}, False), expected)
        for job in ("postgresql-product-proof", "package-product-proof"):
            context = self.admitted_context(job) | {
                "needs.classify.outputs.requires_custom_stack": "false",
                "needs.custom-stack-proof.result": "skipped",
            }
            if job == "package-product-proof":
                context |= {
                    "needs.classify.outputs.requires_postgresql_product": "false",
                    "needs.postgresql-product-proof.result": "skipped",
                }
            self.assertTrue(condition_allows(job_condition(workflow, job), context, False))
            self.assertFalse(condition_allows(job_condition(workflow, job), context, True))

    def test_short_aggregate_and_protected_status_jobs_still_evaluate_after_cancellation(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        for job in ("product-path", "required-source-status", "required-native-status", "required-sanitizer-status"):
            with self.subTest(job=job):
                condition = job_condition(workflow, job)
                self.assertEqual(condition, "always()")
                self.assertTrue(condition_allows(condition, {}, True))

    def test_source_only_main_cannot_enter_installed_jobs(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        for job, flag in (("dev-bat-deployment", "deployment"),
                          ("deployed-stockfish-corpus", "stockfish_corpus")):
            condition = job_condition(workflow, job)
            context = self.admitted_context(job) | {f"needs.classify.outputs.requires_{flag}": "false"}
            self.assertFalse(condition_allows(condition, context, False), job)
            mutant = condition.replace(f" && needs.classify.outputs.requires_{flag} == 'true'", "")
            self.assertTrue(condition_allows(mutant, context, False), job)

    def test_pull_request_runs_cancel_obsolete_active_head_without_cancelling_main(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        marker = (
            "concurrency:\n"
            "  group: product-path-${{ github.ref }}\n"
            "  queue: ${{ github.event_name == 'pull_request' && 'single' || 'max' }}\n"
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
            workflow.index("  dev-bat-live-substrate:") : workflow.index("  required-source-status:")
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
