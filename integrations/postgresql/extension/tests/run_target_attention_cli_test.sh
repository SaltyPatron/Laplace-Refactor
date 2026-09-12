#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 11 ]]; then
    echo "usage: $0 PG-BINDIR CONTROL-ROOT MODULE-DIRECTORY ENGINE-DIRECTORY NATIVE-PROBE PYTHON CLI FRAMEWORK-MAJOR FRAMEWORK-MINOR READ-ONLY-FLAG SANITIZER-PRELOAD" >&2
    exit 64
fi

pg_bindir=$1
postgres_library_directory=$("$pg_bindir/pg_config" --pkglibdir)
control_root=$2
module_directory=$3
engine_directory=$4
native_probe=$5
python=$6
cli=$7
framework_major=$8
framework_minor=$9
read_only_flag=${10}
sanitizer_preload=${11}
temporary_parent=${RUNNER_TEMP:-${TMPDIR:-/tmp}}
mkdir -p -- "$temporary_parent"
test_root=$(mktemp -d "$temporary_parent/laplace-model-export-cli.XXXXXX")
data_directory="$test_root/data"
socket_directory=$(mktemp -d /tmp/lp-mx-pg.XXXXXX)
server_log="$test_root/postgres.log"
request_file="$test_root/request.json"
artifact_one="$test_root/model-one.safetensors"
receipt_one="$test_root/model-one.receipt.json"
artifact_two="$test_root/model-two.safetensors"
receipt_two="$test_root/model-two.receipt.json"
port=${LAPLACE_POSTGRES_TARGET_ATTENTION_CLI_TEST_PORT:-55446}
server_started=0
server_asan_options=${ASAN_OPTIONS:-}
if [[ -n "$sanitizer_preload" ]]; then
    server_asan_options="${server_asan_options}${server_asan_options:+:}detect_leaks=0"
fi

cleanup() {
    exit_code=$?
    if [[ $server_started -eq 1 ]]; then
        "$pg_bindir/pg_ctl" -D "$data_directory" -m immediate -t 5 -w stop \
            >/dev/null 2>&1 || true
    fi
    if [[ "$socket_directory" == /tmp/lp-mx-pg.* ]]; then
        rmdir -- "$socket_directory" 2>/dev/null || true
    fi
    if [[ $exit_code -ne 0 ]]; then
        cat "$server_log" >&2 2>/dev/null || true
    fi
    rm -rf -- "$test_root"
    exit "$exit_code"
}
trap cleanup EXIT

"$pg_bindir/initdb" -D "$data_directory" \
    --no-locale --encoding=UTF8 --auth=trust >/dev/null
postgres_options="-F -k $socket_directory -p $port -c listen_addresses= -c extension_control_path=$control_root -c dynamic_library_path=$module_directory:$postgres_library_directory:$engine_directory"
ASAN_OPTIONS="$server_asan_options" \
LD_PRELOAD="${sanitizer_preload}${sanitizer_preload:+${LD_PRELOAD:+:}}${LD_PRELOAD:-}" \
"$pg_bindir/pg_ctl" -D "$data_directory" -l "$server_log" \
    -o "$postgres_options" -w start >/dev/null
server_started=1

LD_PRELOAD="${sanitizer_preload}${sanitizer_preload:+${LD_PRELOAD:+:}}${LD_PRELOAD:-}" \
"$pg_bindir/psql" -X -h "$socket_directory" -p "$port" -d postgres \
    -v ON_ERROR_STOP=1 -c 'CREATE EXTENSION laplace;' >/dev/null

if [[ ! -x "$native_probe" ]]; then
    echo "target attention native oracle is unavailable" >&2
    exit 65
fi
probe_output=$(
    LD_LIBRARY_PATH="$engine_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$native_probe"
)
scope_receipt=$(awk -F= '$1=="SCOPED_SCOPE_RECEIPT_ID" {print $2}' <<<"$probe_output")
compile_receipt=$(awk -F= '$1=="SCOPED_COMPILE_RECEIPT_ID" {print $2}' <<<"$probe_output")
projection_id=$(awk -F= '$1=="SCOPED_PROJECTION_ID" {print $2}' <<<"$probe_output")
if [[ ! "$scope_receipt" =~ ^[0-9a-f]{64}$ ||
      ! "$compile_receipt" =~ ^[0-9a-f]{64}$ ||
      ! "$projection_id" =~ ^[0-9a-f]{64}$ ]]; then
    echo "native scoped target oracle did not produce valid fingerprints" >&2
    exit 66
fi

"$python" - "$request_file" "$framework_major" "$framework_minor" "$read_only_flag" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
major = int(sys.argv[2])
minor = int(sys.argv[3])
flags = int(sys.argv[4])

def d(value: int) -> str:
    return f"{value:02x}" * 32

def i(value: int) -> str:
    return f"{value:02x}" * 16

def field(seed: int, ordinal: int) -> dict:
    return {
        "field_id": d(seed),
        "entity_id": i(seed + 40),
        "physicality_id": d(0),
        "role_id": d(0),
        "recipe_fingerprint": d(seed + 80),
        "ordinal": ordinal,
        "value_dimension": 1,
        "flags": 0,
    }

def constraint(seed: int, family: int, source: int, target: int, precision: float) -> dict:
    return {
        "constraint_id": d(seed),
        "plane_id": d(seed + 20),
        "law_fingerprint": d(seed + 40),
        "units_fingerprint": d(seed + 60),
        "evidence_root_id": d(0),
        "calculation_receipt_id": d(seed + 80),
        "source_field_index": source,
        "target_field_index": target,
        "transport_scale": 1.0,
        "transport_offset": 0.0,
        "target_value": 0.0,
        "precision": precision,
        "relation_family": family,
        "source_class": 1,
        "direction": 1,
        "transport_kind": 1,
        "flags": 0,
    }

fields = [field(40, 0), field(41, 1), field(42, 2), field(43, 3)]
request = {
    "schema": "laplace.target-attention-export-request/v2",
    "execution_context": {
        "epochs": [d(value) for value in range(1, 11)],
        "authority_fingerprint": d(160),
        "memory_bytes": 1048576,
        "cpu_slots": 4,
        "io_slots": 1,
        "epoch_mask": 1023,
        "framework_major": major,
        "framework_minor": minor,
        "flags": flags,
    },
    "evidence_boundary": d(176),
    "evidence_epoch": d(177),
    "recipe_fingerprint": d(80),
    "target_contract_fingerprint": d(81),
    "operator_numeric_tolerance": 1e-12,
    "operator_program_flags": 7,
    "hidden_width": 2,
    "relative_tolerance": 1e-10,
    "require_exact": True,
    "fields": fields,
    "constraints": [
        constraint(50, 11, 0, 1, 2.0),
        constraint(60, 12, 2, 3, 3.0),
    ],
    "slots": [
        {
            "target_role": "qk",
            "layer_index": 2,
            "head_index": 5,
            "expert_index": 0,
            "eligible_relation_families": [11],
            "eligible_source_mask": 1,
            "head_rank": 1,
            "flags": 0,
        },
        {
            "target_role": "vo",
            "layer_index": 2,
            "head_index": 5,
            "expert_index": 0,
            "eligible_relation_families": [12],
            "eligible_source_mask": 1,
            "head_rank": 1,
            "flags": 0,
        },
    ],
}
path.write_text(json.dumps(request, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
PY

rendered_sql=$("$python" "$cli" "$request_file" --output "$test_root/render-only.safetensors" --render-sql)
if [[ "$rendered_sql" != *"laplace.target_attention_export_scoped"* ]]; then
    echo "model export v2 did not render the scoped PostgreSQL operation" >&2
    exit 67
fi
if [[ "$rendered_sql" == *"target_operator_job"* || "$rendered_sql" == *"operator_program"* ]]; then
    echo "model export v2 reconstructed low-level target jobs/programs in the client" >&2
    exit 68
fi

"$python" "$cli" "$request_file" \
    --psql "$pg_bindir/psql" --host "$socket_directory" --port "$port" \
    --database postgres --output "$artifact_one" --receipt "$receipt_one" >/dev/null
"$python" "$cli" "$request_file" \
    --psql "$pg_bindir/psql" --host "$socket_directory" --port "$port" \
    --database postgres --output "$artifact_two" --receipt "$receipt_two" >/dev/null

cmp -- "$artifact_one" "$artifact_two"
"$python" - "$artifact_one" "$receipt_one" "$receipt_two" "$scope_receipt" "$compile_receipt" "$projection_id" <<'PY'
import json
import sys
from pathlib import Path

artifact = Path(sys.argv[1]).read_bytes()
first = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
second = json.loads(Path(sys.argv[3]).read_text(encoding="utf-8"))
expected_scope = sys.argv[4]
expected_compile = sys.argv[5]
expected_projection = sys.argv[6]
assert len(artifact) > 8
assert first["schema"] == "laplace.target-attention-export-client-receipt/v2"
assert first["request_schema"] == "laplace.target-attention-export-request/v2"
assert first["artifact_id"] == second["artifact_id"]
assert first["artifact_sha256"] == second["artifact_sha256"]
assert first["scope_receipt_id"] == expected_scope
assert first["compile_receipt_id"] == expected_compile
assert first["projection_id"] == expected_projection
assert first["selected_constraint_count"] == 2
assert first["source_mask_union"] == 1
assert first["tensor_count"] == 5
assert first["head_count"] == 1
assert first["field_count"] == 4
assert first["hidden_width"] == 2
header_length = int.from_bytes(artifact[:8], "little")
header = json.loads(artifact[8:8 + header_length].decode("utf-8").rstrip(" "))
for name in (
    "laplace.embedding",
    "laplace.q.layer_2.head_5.expert_0",
    "laplace.k.layer_2.head_5.expert_0",
    "laplace.v.layer_2.head_5.expert_0",
    "laplace.o.layer_2.head_5.expert_0",
):
    assert name in header, name
PY
