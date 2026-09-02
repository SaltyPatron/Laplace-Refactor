#!/usr/bin/env python3
"""Executable governance proof for the Laplace finish-line program."""

from __future__ import annotations

import copy
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROGRAM_PATH = ROOT / "contracts" / "finish-line-program.json"
OPERATION_PATH = ROOT / "contracts" / "operation-model.json"
AUTHORITY_PATH = ROOT / "contracts" / "authority-stack.json"
AGENTS_PATH = ROOT / "AGENTS.md"
FEATURE_PATH = ROOT / "requirements" / "features" / "finish_line_program.feature"

EXPECTED_GITHUB_MILESTONES = {
    0: (1, "Phase 0 — Canonical cutover"),
    1: (3, "Phase 1 — Industrial build and dependency foundation"),
    2: (2, "Phase 2 — Universal execution framework and ISA"),
    3: (4, "Phase 3 — Universal substrate state"),
    4: (5, "Phase 4 — Batch world-state admission"),
    5: (6, "Phase 5 — Universal query and conversation"),
    6: (7, "Phase 6 — Model independence"),
    7: (8, "Phase 7 — Complete product surfaces"),
    8: (9, "Phase 8 — Full acceptance and release"),
}

EXPECTED_SUBSTITUTIONS = {
    "dense-all-pairs-for-sparse-indexed-n-to-k",
    "file-source-record-as-worker-atom",
    "private-etl-or-decomposer-engine-per-source",
    "physical-batch-boundary-as-semantic-boundary",
    "global-adjacency-embedding-nearest-neighbor-or-flat-score-as-machine",
    "bpe-or-fixed-tokenizer-as-native-symbolic-floor",
    "gradient-or-checkpoint-training-as-native-admission-authority",
    "gpu-or-world-model-residency-as-semantic-requirement",
    "sql-cursor-rbar-recursive-or-dynamic-per-candidate-cognition",
    "ui-api-csharp-or-source-adapter-private-semantics",
    "source-path-tier-occurrence-time-worker-or-batch-in-canonical-content-identity",
    "semantic-preflight-replayed-and-discarded-before-real-execution",
    "load-shape-fluency-nonempty-or-historical-result-as-complete-acceptance",
}

EXPECTED_DELIVERED_CHAIN = {
    "inventor-or-stable-product-law",
    "versioned-contract-or-requirement",
    "one-semantic-implementation-owner",
    "positive-executable-acceptance",
    "deliberate-defect-or-mutation-sensitivity",
    "provider-and-route-parity-when-applicable",
    "package-install-activation-identity-when-applicable",
    "durable-or-public-readback-when-applicable",
    "representative-physical-plan-and-performance-receipt-when-applicable",
    "owning-milestone-exit-predicate",
}

MANDATORY_PROGRAM_OWNERS = {22, 23, 180, 4, 10, 177, 179, 171, 165, 166, 167}


def load(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path} is not an object")
    return value


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def validate_program(
    program: dict,
    operation: dict,
    authority: dict,
    agents_text: str,
    feature_text: str,
) -> None:
    require(program.get("schema") == "laplace.finish-line-program/v1", "finish-line schema drift")
    require(program.get("repository") == "SaltyPatron/Laplace-Refactor", "finish-line repository drift")

    program_authority = program.get("program_authority", {})
    require(program_authority.get("whole_product_issue") == 23, "whole-product owner is not #23")
    require(program_authority.get("governance_issue") == 180, "governance owner is not #180")
    require(program_authority.get("complete_product_acceptance_issue") == 22, "complete-product gate is not #22")
    require(program_authority.get("finish_line_kind") == "whole-product-contract", "a branch or PR was promoted to finish-line authority")
    require(program_authority.get("pull_request_is_never_product_finish_line") is True, "PR can impersonate product finish line")

    terminal = program.get("terminal_definition", {})
    delivered_chain = terminal.get("delivered_chain", [])
    require(set(delivered_chain) == EXPECTED_DELIVERED_CHAIN, "delivered evidence chain lost a required class")
    non_proofs = set(terminal.get("non_proofs", []))
    require({"issue-count", "pull-request-count", "green-component-test", "historical-capability"}.issubset(non_proofs), "component/meta progress can impersonate completion")

    state_machine = program.get("work_state_machine", {})
    require(set(state_machine.get("states", [])) == {"planned", "active", "blocked", "failed-acceptance", "delivered", "superseded"}, "work state machine drift")
    require("exit_condition" in state_machine.get("active_requires", []), "active work can lack an executable exit")
    require("immediate_next_action" in state_machine.get("active_requires", []), "active work can lack a next action")

    milestones = program.get("milestones", [])
    require(isinstance(milestones, list) and len(milestones) == 9, "finish-line must bind exactly nine program phases")
    by_phase = {entry.get("phase"): entry for entry in milestones}
    require(set(by_phase) == set(range(9)), "finish-line phase set drift")
    milestone_numbers: set[int] = set()
    mandatory = set(program.get("mandatory_capability_owners", []))
    require(MANDATORY_PROGRAM_OWNERS.issubset(mandatory), "mandatory current program owner disappeared")

    operation_milestones = operation.get("tracking", {}).get("phase_milestones", {})
    require(set(operation_milestones) == {str(index) for index in range(9)}, "operation model phase coverage drift")

    for phase, expected in EXPECTED_GITHUB_MILESTONES.items():
        entry = by_phase[phase]
        expected_number, expected_title = expected
        require(entry.get("github_milestone_number") == expected_number, f"phase {phase} GitHub milestone number drift")
        require(entry.get("github_title") == expected_title, f"phase {phase} GitHub milestone title drift")
        require(entry.get("operation_model_title") == operation_milestones[str(phase)], f"phase {phase} operation-model binding drift")
        require(isinstance(entry.get("exit_predicate"), str) and entry["exit_predicate"].strip(), f"phase {phase} lacks exit predicate")
        require(isinstance(entry.get("evidence_boundary"), str) and entry["evidence_boundary"].strip(), f"phase {phase} lacks evidence boundary")
        milestone_numbers.add(expected_number)
        owners = entry.get("primary_owners", [])
        require(owners and set(owners).issubset(mandatory), f"phase {phase} owner is not in mandatory owner graph")
    require(len(milestone_numbers) == 9, "GitHub milestones are not uniquely bound")

    operational_graph = program.get("operational_graph", {})
    require(operational_graph.get("contract") == "contracts/operation-model.json", "finish line lost operation-model join")
    require(operational_graph.get("phase_count") == 9, "operational phase count drift")
    stages = operation.get("stages", [])
    require(isinstance(stages, list) and stages, "operation model has no stages")
    for stage in stages:
        require(isinstance(stage.get("id"), str) and stage["id"], "operation stage lacks id")
        phases = stage.get("program_phases", [])
        require(phases and all(phase in by_phase for phase in phases), f"operation stage {stage.get('id')} escapes finish-line phases")

    anti = program.get("anti_substitution_controls", {})
    require(set(anti.get("required", [])) == EXPECTED_SUBSTITUTIONS, "anti-substitution matrix lost or gained unreviewed control")
    require("deliberate-defect" in anti.get("coverage_rule", ""), "anti-substitution controls can close on prose alone")

    rework = program.get("rework_escalation", {})
    require(rework.get("repeat_leaf_threshold") == 2, "rework escalation no longer triggers on repeated independent leaves")
    require(len(rework.get("required_actions", [])) >= 5, "generic-owner repair law was weakened")
    require("file-grain-parallel-scheduling" in rework.get("known_classes", []), "known scheduler defect disappeared from rework controls")
    require("duplicate-semantic-calculation-in-planning-and-execution" in rework.get("known_classes", []), "known duplicate-calculation defect disappeared from rework controls")

    sprints = program.get("current_sprints", [])
    active_sprints = [sprint for sprint in sprints if sprint.get("state") == "active"]
    require(len(active_sprints) == 1, "exactly one current bounded sprint must be active")
    sprint = active_sprints[0]
    items = sprint.get("items", [])
    require(items, "active sprint is empty")
    issue_ids = [item.get("issue") for item in items]
    require(len(issue_ids) == len(set(issue_ids)), "active sprint duplicates an issue")
    require({4, 10, 177, 179, 171}.issubset(set(issue_ids)), "substrate execution correctness sprint lost a required owner")
    for item in items:
        require(item.get("issue") in mandatory, f"sprint issue {item.get('issue')} is not a mandatory owner")
        phase = item.get("phase")
        require(phase in by_phase, f"sprint issue {item.get('issue')} has unknown phase")
        require(item.get("milestone") == by_phase[phase]["github_milestone_number"], f"sprint issue {item.get('issue')} milestone/phase mismatch")
        require(isinstance(item.get("dependencies"), list), f"sprint issue {item.get('issue')} lacks dependencies")
        require(isinstance(item.get("exit_condition"), str) and item["exit_condition"].strip(), f"sprint issue {item.get('issue')} lacks exit condition")
        require(isinstance(item.get("immediate_next_action"), str) and item["immediate_next_action"].strip(), f"sprint issue {item.get('issue')} lacks immediate next action")
        require(all(dep in mandatory for dep in item["dependencies"]), f"sprint issue {item.get('issue')} has undeclared dependency owner")

    correction = program.get("direct_correction_capture", {})
    require(correction.get("historical_audit_issue") == 24, "historical correction boundary lost")
    require(correction.get("assistant_hypothesis_is_authority") is False, "assistant hypothesis can manufacture inventor authority")
    require(correction.get("unresolved_direct_correction_blocks_complete_claim") is True, "unresolved direct correction no longer blocks completion")

    benchmark = program.get("benchmark_truth", {})
    legacy_single = benchmark.get("legacy_single_thread", {})
    require(legacy_single.get("repository") == "SaltyPatron/Laplace", "legacy benchmark provenance drift")
    require(legacy_single.get("may_be_attributed_to_refactor") is False, "legacy performance can be attributed to refactor")
    require(benchmark.get("legacy_file_grain_scale", {}).get("may_be_used_as_whole_machine_ceiling") is False, "file-grain makespan can impersonate machine ceiling")
    require(benchmark.get("independent_stream_scale", {}).get("proves_single_semantic_dag_parallelism") is False, "replicated streams can impersonate single-DAG parallelism")
    require(benchmark.get("refactor_benchmarks", {}).get("owner_issue") == 166, "refactor benchmark owner drift")
    require("hide-a-second-semantic-calculation-pass" in benchmark.get("forbidden_speedup_methods", []), "hidden duplicate semantic work can manufacture speedup")

    issue_contract = program.get("issue_pr_contract", {})
    require("conventional_substitution_rejected" in issue_contract.get("required_implementation_pr_fields", []), "PRs need not name the attractive wrong substitute")
    require("deliberate_defect" in issue_contract.get("required_implementation_pr_fields", []), "implementation PRs can omit mutation sensitivity")

    order = authority.get("required_load_order", [])
    paths = [entry.get("path") for entry in order]
    require("contracts/finish-line-program.json" in paths, "finish-line program is absent from authority load order")
    finish_index = paths.index("contracts/finish-line-program.json")
    require(finish_index < paths.index("docs/product/ROADMAP.md"), "mutable roadmap loads before finish-line program")
    require(finish_index < paths.index("state/continuation.json"), "observed continuation state loads before finish-line program")

    require("contracts/finish-line-program.json" in agents_text, "AGENTS projection does not require finish-line program")
    require("conventional substitution" in agents_text.lower(), "AGENTS projection lost anti-substitution work selection")
    require("generic owner" in agents_text.lower(), "AGENTS projection lost repeated-defect escalation")

    require(FEATURE_PATH.is_file(), "finish-line BDD feature is missing")
    require("@LP-TEST-CONTINUATION-AUTHORITY" in feature_text, "finish-line feature lost authority evidence target")
    require("Repeated leaf failures escalate to the generic owner" in feature_text, "finish-line feature lost rework escalation scenario")
    require("Performance claims bind semantic work and physical plan" in feature_text, "finish-line feature lost benchmark truth scenario")


class FinishLineProgramTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.program = load(PROGRAM_PATH)
        cls.operation = load(OPERATION_PATH)
        cls.authority = load(AUTHORITY_PATH)
        cls.agents_text = AGENTS_PATH.read_text(encoding="utf-8")
        cls.feature_text = FEATURE_PATH.read_text(encoding="utf-8")

    def validate(self, program: dict) -> None:
        validate_program(program, self.operation, self.authority, self.agents_text, self.feature_text)

    def test_current_program_contract_is_complete(self) -> None:
        self.validate(self.program)

    def test_mutation_missing_whole_product_owner_fails(self) -> None:
        mutant = copy.deepcopy(self.program)
        mutant["mandatory_capability_owners"].remove(23)
        with self.assertRaises(ValueError):
            self.validate(mutant)

    def test_mutation_conventional_substitution_removed_fails(self) -> None:
        mutant = copy.deepcopy(self.program)
        mutant["anti_substitution_controls"]["required"].remove("file-source-record-as-worker-atom")
        with self.assertRaises(ValueError):
            self.validate(mutant)

    def test_mutation_active_item_without_exit_fails(self) -> None:
        mutant = copy.deepcopy(self.program)
        item = next(entry for entry in mutant["current_sprints"][0]["items"] if entry["issue"] == 177)
        item["exit_condition"] = ""
        with self.assertRaises(ValueError):
            self.validate(mutant)

    def test_mutation_github_milestone_identity_fails(self) -> None:
        mutant = copy.deepcopy(self.program)
        mutant["milestones"][3]["github_title"] = "Phase 3 — Something familiar"
        with self.assertRaises(ValueError):
            self.validate(mutant)

    def test_mutation_pr_promoted_to_finish_line_fails(self) -> None:
        mutant = copy.deepcopy(self.program)
        mutant["program_authority"]["finish_line_kind"] = "pull-request"
        mutant["program_authority"]["pull_request_is_never_product_finish_line"] = False
        with self.assertRaises(ValueError):
            self.validate(mutant)

    def test_mutation_deliberate_defect_removed_from_delivery_fails(self) -> None:
        mutant = copy.deepcopy(self.program)
        mutant["terminal_definition"]["delivered_chain"].remove("deliberate-defect-or-mutation-sensitivity")
        with self.assertRaises(ValueError):
            self.validate(mutant)


if __name__ == "__main__":
    unittest.main(verbosity=2)
