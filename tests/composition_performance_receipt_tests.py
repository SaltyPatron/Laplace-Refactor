#!/usr/bin/env python3
"""Validate whole-boundary composition measurements and their defect controls."""

from __future__ import annotations

import copy
import json
import pathlib
import sys


EXPECTED = {
    "known_entities": 2,
    "requests": 65_812,
    "operands": 131_624,
    "entities": 65_814,
    "physicalities": 65_812,
    "trajectory_vertices": 131_624,
    "occurrences": 65_812,
    "stream_records": 329_062,
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def digest(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdef" for character in value)
    )


def validate(receipt: dict[str, object]) -> None:
    require(
        receipt.get("schema")
        == "laplace.composition-whole-boundary-measurement/v1",
        "composition measurement schema changed",
    )
    require(
        receipt.get("generator")
        == "tests/postgres/composition_measurement_client.cpp",
        "measurement generator is not identified",
    )
    require(
        receipt.get("source_contract") == "contracts/composition.json",
        "measurement source contract is not identified",
    )
    require(
        receipt.get("proof_state")
        == "isolated-integration-measurement-not-product-activation",
        "integration measurement was promoted to product state",
    )
    for field in (
        "timing_boundary",
        "cache_state",
        "durability_mode",
        "memory_boundary",
        "command",
        "postgresql_version",
    ):
        require(isinstance(receipt.get(field), str) and receipt[field],
                f"{field} is missing")
    require("fsync-on" in receipt["durability_mode"],
            "measurement did not require durable PostgreSQL settings")

    machine = receipt.get("machine")
    require(isinstance(machine, dict), "machine identity is missing")
    for field in ("sysname", "release", "architecture", "cpu_model"):
        require(isinstance(machine.get(field), str) and machine[field],
                f"machine {field} is missing")
    for field in ("online_processors", "page_bytes"):
        require(isinstance(machine.get(field), int) and machine[field] > 0,
                f"machine {field} is invalid")

    input_shape = receipt.get("input")
    require(isinstance(input_shape, dict), "input shape is missing")
    for field in ("known_entities", "requests", "operands"):
        require(input_shape.get(field) == EXPECTED[field],
                f"input {field} changed")
    require(
        isinstance(input_shape.get("postgresql_stored_input_datum_bytes"), int)
        and input_shape["postgresql_stored_input_datum_bytes"] > 0,
        "PostgreSQL stored input datum byte count is missing",
    )
    require(input_shape.get("preferred_batch_bytes") == 8_388_608,
            "batch boundary changed")
    require(receipt.get("expected_durable_counts") == {
        field: EXPECTED[field]
        for field in (
            "entities", "physicalities", "trajectory_vertices",
            "occurrences", "stream_records"
        )
    }, "durable denominator changed")

    samples = receipt.get("samples")
    require(isinstance(samples, list) and len(samples) >= 3,
            "at least three real samples are required")
    require(receipt.get("sample_count") == len(samples),
            "sample count does not match samples")
    semantic_receipt = None
    working_set_receipt = None
    stream_fingerprint = None
    for ordinal, sample in enumerate(samples):
        require(sample.get("ordinal") == ordinal, "sample ordering changed")
        for field in (
            "wall_nanoseconds", "backend_high_water_bytes",
            "estimated_peak_working_bytes", "wal_bytes", "database_calls",
            "durable_outputs", "stream_bytes",
        ):
            require(isinstance(sample.get(field), int) and sample[field] > 0,
                    f"sample {ordinal} {field} is not positive")
        for field in (
            "user_cpu_microseconds", "system_cpu_microseconds",
            "rss_before_bytes", "rss_after_bytes", "read_syscalls",
            "write_syscalls", "filesystem_read_bytes",
            "filesystem_write_bytes",
        ):
            require(isinstance(sample.get(field), int) and sample[field] >= 0,
                    f"sample {ordinal} {field} is invalid")
        require(
            sample["user_cpu_microseconds"] + sample["system_cpu_microseconds"] > 0,
            f"sample {ordinal} has no measured server CPU",
        )
        require(sample["database_calls"] == 17,
                f"sample {ordinal} did not use five tier presence calls, one "
                "physicality call, and eleven persistence plans")
        require(sample["durable_outputs"] == EXPECTED["stream_records"] + 1,
                f"sample {ordinal} durable readback denominator changed")
        for field in (
            "working_set_receipt", "presence_semantic_receipt",
            "presence_execution_receipt", "stream_fingerprint",
        ):
            require(digest(sample.get(field)),
                    f"sample {ordinal} {field} is not a digest")
        if ordinal == 0:
            semantic_receipt = sample["presence_semantic_receipt"]
            working_set_receipt = sample["working_set_receipt"]
            stream_fingerprint = sample["stream_fingerprint"]
        else:
            require(sample["presence_semantic_receipt"] == semantic_receipt,
                    "semantic presence changed between identical fresh samples")
            require(sample["working_set_receipt"] == working_set_receipt,
                    "working-set receipt changed between identical fresh samples")
            require(sample["stream_fingerprint"] == stream_fingerprint,
                    "publication stream changed between identical fresh samples")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: composition-receipt-test RECEIPT")
    receipt = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
    validate(receipt)

    no_database_calls = copy.deepcopy(receipt)
    no_database_calls["samples"][0]["database_calls"] = 0
    try:
        validate(no_database_calls)
    except ValueError:
        pass
    else:
        raise AssertionError("missing database-call evidence was accepted")

    nondeterministic = copy.deepcopy(receipt)
    nondeterministic["samples"][1]["presence_semantic_receipt"] = "00" * 32
    try:
        validate(nondeterministic)
    except ValueError:
        pass
    else:
        raise AssertionError("semantic receipt drift was accepted")

    no_wall_boundary = copy.deepcopy(receipt)
    no_wall_boundary["samples"][0]["wall_nanoseconds"] = 0
    try:
        validate(no_wall_boundary)
    except ValueError:
        pass
    else:
        raise AssertionError("missing wall-time measurement was accepted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
