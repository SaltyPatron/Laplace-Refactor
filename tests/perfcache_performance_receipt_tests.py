#!/usr/bin/env python3
"""Validate the measured Unicode perfcache receipt and its defect controls."""

from __future__ import annotations

import copy
import json
import pathlib
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def is_hex(value: object, byte_count: int) -> bool:
    if not isinstance(value, str) or len(value) != byte_count * 2:
        return False
    return all(character in "0123456789abcdef" for character in value)


def validate(contract: dict[str, object], receipt: dict[str, object]) -> None:
    performance = contract["hot_lookup_performance"]
    require(isinstance(performance, dict), "performance contract must be an object")
    require(receipt.get("schema") == performance["receipt_schema"],
            "receipt schema differs from the generated contract")
    require(receipt.get("state") == performance["state"],
            "measured state differs from the generated contract")
    require(receipt.get("timing_boundary") == performance["timing_boundary"],
            "timing boundary differs from the generated contract")
    require(receipt.get("sample_count") == performance["sample_count"],
            "sample count differs from the generated contract")
    require(receipt.get("warmup_batch_count") == performance["warmup_batch_count"],
            "warmup count differs from the generated contract")

    for field in (
        "tier0_artifact_digest", "reverse_artifact_digest",
        "activation_epoch_fingerprint", "source_fingerprint",
        "recipe_fingerprint", "tier0_module_contract_fingerprint",
        "reverse_module_contract_fingerprint", "result_fingerprint",
    ):
        require(is_hex(receipt.get(field), 32), f"{field} is not a digest")
    require(is_hex(receipt.get("activation_epoch_id"), 16),
            "activation epoch ID is not a content ID")
    require(receipt.get("mapped_bytes") == receipt.get("prefaulted_bytes"),
            "the complete mapped generation was not prefaulted")
    for field in ("tier0_artifact_bytes", "reverse_artifact_bytes"):
        require(isinstance(receipt.get(field), int) and receipt[field] > 0,
                f"{field} must be a positive measured value")
    require(receipt["mapped_bytes"] ==
            receipt["tier0_artifact_bytes"] + receipt["reverse_artifact_bytes"],
            "mapped bytes do not equal the complete direct/reverse artifact set")
    require(isinstance(receipt.get("prefaulted_pages"), int) and
            receipt["prefaulted_pages"] > 0,
            "prefault page count is missing")
    for field in (
        "allowed_processors", "memory_domains", "usable_memory_bytes",
        "page_bytes", "wall_nanoseconds", "maximum_resident_bytes",
    ):
        require(isinstance(receipt.get(field), int) and receipt[field] > 0,
                f"{field} must be a positive measured value")
    for field in (
        "topology_flags", "isa_flags", "user_cpu_microseconds",
        "system_cpu_microseconds", "filesystem_input_blocks",
        "filesystem_output_blocks", "database_calls", "durable_outputs",
    ):
        require(isinstance(receipt.get(field), int) and receipt[field] >= 0,
                f"{field} must be a nonnegative measured value")
    require(receipt["database_calls"] == 0 and receipt["durable_outputs"] == 0,
            "the native hot lookup boundary performed database or durable effects")
    require(isinstance(receipt.get("compiler"), str) and receipt["compiler"],
            "toolchain identity is missing")
    expected_pages = sum(
        (receipt[field] + receipt["page_bytes"] - 1) // receipt["page_bytes"]
        for field in ("tier0_artifact_bytes", "reverse_artifact_bytes")
    )
    require(receipt["prefaulted_pages"] == expected_pages,
            "prefault page count does not cover both complete artifacts")

    operations = receipt.get("operations")
    require(isinstance(operations, list), "operations must be an array")
    widths = performance["batch_widths"]
    require([operation.get("width") for operation in operations] == widths,
            "measured batch widths differ from the generated contract")
    for operation in operations:
        width = operation["width"]
        require(operation.get("hits_per_sample") == width and
                operation.get("misses_per_sample") == 0,
                "hot lookup output cardinality is not exact")
        for prefix in ("direct", "reverse"):
            p50 = operation[f"{prefix}_p50_batch_ns"]
            p95 = operation[f"{prefix}_p95_batch_ns"]
            p99 = operation[f"{prefix}_p99_batch_ns"]
            require(isinstance(p50, int) and 0 < p50 <= p95 <= p99,
                    f"{prefix} percentiles are invalid")
            require(operation[f"{prefix}_p50_ns_per_key"] == p50 // width,
                    f"{prefix} per-key time is inconsistent")
            require(operation[f"{prefix}_p50_keys_per_second"] ==
                    1_000_000_000 * width // p50,
                    f"{prefix} throughput is inconsistent")


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: receipt-test CONTRACT RECEIPT")
    contract_path = pathlib.Path(sys.argv[1])
    receipt_path = pathlib.Path(sys.argv[2])
    contract = json.loads(contract_path.read_text(encoding="utf-8"))
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    validate(contract, receipt)

    missing_boundary = copy.deepcopy(receipt)
    missing_boundary.pop("timing_boundary")
    try:
        validate(contract, missing_boundary)
    except ValueError:
        pass
    else:
        raise AssertionError("missing timing boundary was accepted")

    inverted_tail = copy.deepcopy(receipt)
    inverted_tail["operations"][0]["direct_p95_batch_ns"] = 0
    try:
        validate(contract, inverted_tail)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid percentile ordering was accepted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
