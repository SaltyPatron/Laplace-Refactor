#!/usr/bin/env python3
import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = ROOT / "contracts" / "branch-estate-closure.json"
LEDGER_PATH = ROOT / "state" / "branch-estate-ledger.json"

REQUIRED_DISPOSITIONS = {
    "ACTIVE_PR",
    "MERGED",
    "REUSE",
    "ADAPT",
    "SUPERSEDED",
    "RETIRE",
    "ARCHIVE",
    "UNACCOUNTED_UNIQUE_BRANCH",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def validate(contract: dict, ledger: dict) -> None:
    require(contract.get("schema") == "laplace.branch-estate-closure/v1", "wrong branch-estate contract schema")
    require(ledger.get("schema") == "laplace.branch-estate-ledger/v1", "wrong branch-estate ledger schema")
    require(ledger.get("contract") == "contracts/branch-estate-closure.json", "ledger does not bind branch-estate contract")
    require(contract["authority"]["authoritative_branch"] == "main", "main must remain authoritative")
    require(contract["authority"]["closure_issue"] == 183, "#183 must own branch-estate closure")

    allowed = set(contract["dispositions"]["allowed"])
    require(REQUIRED_DISPOSITIONS <= allowed, "branch disposition set lost a required state")
    require("UNACCOUNTED_UNIQUE_BRANCH" in set(contract["dispositions"]["failure_dispositions"]), "unaccounted branch must remain a failure state")

    requirements = contract["dispositions"]["requirements"]
    for disposition in REQUIRED_DISPOSITIONS:
        require(disposition in requirements, f"missing field law for {disposition}")

    open_prs = ledger.get("open_pr_heads", [])
    require(ledger["observed_summary"]["open_pr_head_count"] == len(open_prs), "open PR count drift")
    for item in open_prs:
        require(item.get("disposition") == "ACTIVE_PR", f"open PR {item.get('branch')} is not ACTIVE_PR")
        for field in requirements["ACTIVE_PR"]:
            require(item.get(field) not in (None, "", []), f"open PR {item.get('branch')} missing {field}")

    unresolved = ledger.get("unaccounted_unique_tips", [])
    require(ledger["observed_summary"]["live_unique_no_open_pr_tip_count"] == len(unresolved), "unaccounted branch count drift")
    for item in unresolved:
        require(item.get("disposition") == "UNACCOUNTED_UNIQUE_BRANCH", f"unresolved branch {item.get('branch')} was silently promoted")
        for field in requirements["UNACCOUNTED_UNIQUE_BRANCH"]:
            require(item.get(field) not in (None, "", []), f"unresolved branch missing {field}")

    resolved = ledger.get("resolved_tips", [])
    for item in resolved:
        disposition = item.get("disposition")
        require(disposition in allowed - {"UNACCOUNTED_UNIQUE_BRANCH", "ACTIVE_PR"}, f"resolved entry has invalid disposition {disposition}")
        for field in requirements[disposition]:
            require(item.get(field) not in (None, "", []), f"{item.get('branch')} {disposition} missing {field}")

    counters = ledger.get("terminal_counters", {})
    expected_counter_names = set(contract["terminal_zero_counters"])
    require(set(counters) == expected_counter_names, "terminal counter set drift")
    for name, value in counters.items():
        require(isinstance(value, int) and value >= 0, f"terminal counter {name} must be a nonnegative integer")

    require(counters["unaccounted_unique_branch_tips"] == len(unresolved), "terminal unaccounted count does not match ledger")
    require(counters["open_prs_not_green_and_merged"] >= len(open_prs), "active PRs cannot disappear from closure accounting")
    require(counters["unreconciled_branch_only_implementation_candidates"] == len(ledger.get("confirmed_branch_only_findings", [])), "branch-only implementation candidate count drift")

    zero = all(value == 0 for value in counters.values())
    require(ledger["observed_summary"]["terminal_ready"] is zero, "terminal_ready must equal the all-zero branch-estate condition")
    if unresolved:
        require(not ledger["observed_summary"]["terminal_ready"], "unaccounted unique work cannot be terminal-ready")


def expect_failure(contract: dict, ledger: dict, mutate, name: str) -> None:
    contract_copy = copy.deepcopy(contract)
    ledger_copy = copy.deepcopy(ledger)
    mutate(contract_copy, ledger_copy)
    try:
        validate(contract_copy, ledger_copy)
    except AssertionError:
        return
    raise AssertionError(f"deliberate defect was not detected: {name}")


def main() -> None:
    contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
    ledger = json.loads(LEDGER_PATH.read_text(encoding="utf-8"))
    validate(contract, ledger)

    expect_failure(
        contract,
        ledger,
        lambda _c, l: l["observed_summary"].__setitem__("terminal_ready", True),
        "terminal-ready with nonzero counters",
    )

    def active_pr_without_route(_c, l):
        l["open_pr_heads"] = [
            {
                "branch": "mutation/active-pr-without-route",
                "pr_number": 999999,
                "owner_issue": 183,
                "disposition": "ACTIVE_PR",
                "route_to_main": "",
                "exit_condition": "deliberate mutation fixture",
            }
        ]
        l["observed_summary"]["open_pr_head_count"] = 1
        l["terminal_counters"]["open_prs_not_green_and_merged"] = 1
    expect_failure(contract, ledger, active_pr_without_route, "active PR without route to main")

    def silently_retire(_c, l):
        item = l["unaccounted_unique_tips"].pop(0)
        item["disposition"] = "RETIRE"
        l.setdefault("resolved_tips", []).append(item)
        l["observed_summary"]["live_unique_no_open_pr_tip_count"] -= 1
        l["terminal_counters"]["unaccounted_unique_branch_tips"] -= 1
        l["terminal_counters"]["active_branch_without_resolved_disposition"] -= 1
    expect_failure(contract, ledger, silently_retire, "silent retirement of branch-only behavior")

    def fake_merged(_c, l):
        item = l["unaccounted_unique_tips"].pop(0)
        item["disposition"] = "MERGED"
        item["reconciled_behavior"] = "claimed"
        l.setdefault("resolved_tips", []).append(item)
        l["observed_summary"]["live_unique_no_open_pr_tip_count"] -= 1
        l["terminal_counters"]["unaccounted_unique_branch_tips"] -= 1
        l["terminal_counters"]["active_branch_without_resolved_disposition"] -= 1
    expect_failure(contract, ledger, fake_merged, "merged without authoritative main commit")

    def allow_unknown_without_failure(c, _l):
        c["dispositions"]["failure_dispositions"].remove("UNACCOUNTED_UNIQUE_BRANCH")
    expect_failure(contract, ledger, allow_unknown_without_failure, "unaccounted unique branch no longer a failure")

    print("BRANCH_ESTATE_CLOSURE_OK")


if __name__ == "__main__":
    main()
