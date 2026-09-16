#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 9 ]]; then
    echo "usage: $0 MODE PG-BINDIR CONTROL-ROOT MODULE-DIRECTORY ENGINE-DIRECTORY NATIVE-PROBE AUXILIARY-PROBE SQL-FILE SANITIZER-PRELOAD" >&2
    exit 64
fi

mode=$1
pg_bindir=$2
postgres_library_directory=$("$pg_bindir/pg_config" --pkglibdir)
control_root=$3
module_directory=$4
engine_directory=$5
native_probe=$6
auxiliary_probe=$7
sql_file=$8
sanitizer_preload=$9
temporary_parent=${TMPDIR:-/build/laplace/work}
test_root=
data_directory=
socket_directory=
server_log=
port=${LAPLACE_POSTGRES_TEST_PORT:-55432}
server_started=0
perfcache_root=
chess_evidence_directory=
chess_phase=configuration
server_asan_options=${ASAN_OPTIONS:-}
if [[ -n "$sanitizer_preload" ]]; then
    server_asan_options="${server_asan_options}${server_asan_options:+:}detect_leaks=0"
fi

collect_process_tree() {
    local pid=$1
    local children_file="/proc/$pid/task/$pid/children"
    local child
    if [[ -r "$children_file" ]]; then
        for child in $(<"$children_file"); do
            collect_process_tree "$child"
        done
    fi
    printf '%s\n' "$pid"
}

cleanup() {
    exit_code=$?
    test_processes=()
    if [[ -n "$data_directory" && -r "$data_directory/postmaster.pid" ]]; then
        postmaster_pid=$(head -n 1 -- "$data_directory/postmaster.pid" || true)
        if [[ "$postmaster_pid" =~ ^[0-9]+$ ]]; then
            mapfile -t test_processes < <(collect_process_tree "$postmaster_pid" | sort -un)
        fi
    fi
    if [[ $server_started -eq 1 ]]; then
        if ! "$pg_bindir/pg_ctl" -D "$data_directory" -m fast -t 5 -w stop \
            >/dev/null 2>&1; then
            "$pg_bindir/pg_ctl" -D "$data_directory" -m immediate -t 5 -w stop \
                >/dev/null 2>&1 || true
        fi
    fi
    for pid in "${test_processes[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -KILL "$pid" 2>/dev/null || true
        fi
    done
    if [[ -n "$chess_evidence_directory" && -n "$test_root" ]]; then
        # Export only bounded named evidence, before disposable cluster cleanup.
        # A guard kill can leave no native receipt; retain that absence honestly.
        if ! python3 - "$test_root" "$chess_evidence_directory" "$exit_code" "$chess_phase" <<'PY'
import hashlib
import json
from pathlib import Path
import shutil
import sys

source, target = (Path(value) for value in sys.argv[1:3])
artifacts = {}
for name in (
    "unicode-resource-guard.json", "resource-guard.json",
    "chess-line-resource-guard.json", "chess-line-receipt.json",
    "chess-line-observations.json", "postgres.log",
):
    path = source / name
    if not path.exists():
        continue
    if path.is_symlink() or not path.is_file():
        raise SystemExit("chess evidence is not a regular file: " + name)
    size = path.stat().st_size
    destination = target / name
    if name == "postgres.log":
        # Server diagnostics may be large; retain a declared bounded suffix.
        offset = max(0, size - 8 * 1024 * 1024)
        with path.open("rb") as stream:
            stream.seek(offset)
            payload = stream.read(8 * 1024 * 1024)
        destination.write_bytes(payload)
    else:
        if size > 32 * 1024 * 1024:
            raise SystemExit("chess evidence exceeds its byte bound: " + name)
        offset = 0
        shutil.copyfile(path, destination)
        payload = destination.read_bytes()
    artifacts[name] = {
        "bytes": len(payload), "source_bytes": size, "source_offset": offset,
        "sha256": hashlib.sha256(payload).hexdigest(),
    }
receipt = {
    "schema": "laplace.chess-line-postgres-runner/v1",
    "exit_code": int(sys.argv[3]), "last_phase": sys.argv[4],
    "status": "passed" if int(sys.argv[3]) == 0 and sys.argv[4] == "completed" else "failed",
    "artifacts": artifacts,
}
encoded = json.dumps(receipt, sort_keys=True, separators=(",", ":"))
(target / "runner-receipt.json").write_text(encoded + "\n", encoding="utf-8")
print("LAPLACE_QA_RECEIPT chess_line_postgres_runner " + encoded)
PY
        then
            echo "could not retain complete chess-line evidence at $chess_evidence_directory" >&2
            if [[ $exit_code -eq 0 ]]; then exit_code=92; fi
        else
            echo "Chess LINE PostgreSQL evidence retained at $chess_evidence_directory"
        fi
    fi
    if [[ -n "$socket_directory" && "$socket_directory" == "$temporary_parent"/lp-pg.* ]]; then
        rm -rf -- "$socket_directory"
    fi
    if [[ -z "$test_root" ]]; then
        exit "$exit_code"
    elif [[ "$test_root" != "$temporary_parent"/laplace-postgres-test.* ]]; then
        echo "refusing to clean unexpected PostgreSQL test root: $test_root" >&2
        exit 91
    fi
    if [[ $exit_code -ne 0 &&
          "${LAPLACE_POSTGRES_TEST_RETAIN_ON_FAILURE:-0}" == 1 &&
          "${GITHUB_ACTIONS:-false}" != true ]]; then
        echo "PostgreSQL test evidence retained at $test_root" >&2
    else
        rm -rf -- "$test_root"
    fi
    exit "$exit_code"
}
trap cleanup EXIT

if [[ "$temporary_parent" != /* || ! -d "$temporary_parent" || ! -w "$temporary_parent" ]]; then
    echo "PostgreSQL tests require an existing writable absolute TMPDIR (default /build/laplace/work)." >&2
    exit 72
fi
temporary_parent=$(realpath -e -- "$temporary_parent")
test_root=$(mktemp -d "$temporary_parent/laplace-postgres-test.XXXXXX")
data_directory="$test_root/data"
server_log="$test_root/postgres.log"
perfcache_root="$test_root/perfcache-root"
socket_directory=$(mktemp -d "$temporary_parent/lp-pg.XXXXXX")
socket_path_bytes=$(LC_ALL=C printf '%s' "$socket_directory/.s.PGSQL.$port" | wc -c)
if (( socket_path_bytes >= 104 )); then
    echo "PostgreSQL socket path is too long; select a shorter dedicated TMPDIR." >&2
    exit 64
fi
mkdir -p -- "$perfcache_root"

if [[ "$mode" == "chess-line" ]]; then
    # Keep the existing nine-argument interface: source probe, then chess probe.
    chess_manifest=${LAPLACE_CHESS_LINE_MANIFEST:-}
    chess_pgn=${LAPLACE_CHESS_LINE_PGN:-}
    chess_native_engine=${LAPLACE_CHESS_LINE_NATIVE_ENGINE:-}
    chess_evidence_root=${LAPLACE_CHESS_LINE_EVIDENCE_ROOT:-}
    for input in "$chess_manifest" "$chess_pgn" "$chess_native_engine" "$sql_file"; do
        if [[ "$input" != /* || ! -f "$input" || ! -r "$input" ]]; then
            echo "chess-line requires readable absolute manifest, PGN, native-engine and SQL paths" >&2
            exit 64
        fi
    done
    for probe in "$native_probe" "$auxiliary_probe"; do
        if [[ "$probe" != /* || ! -x "$probe" || ! -f "$probe" ]]; then
            echo "chess-line requires actual executable source and chess probes" >&2
            exit 64
        fi
    done
    if [[ "$chess_evidence_root" != /* || ! -d "$chess_evidence_root" || ! -w "$chess_evidence_root" ]]; then
        echo "chess-line requires an existing writable absolute LAPLACE_CHESS_LINE_EVIDENCE_ROOT" >&2
        exit 64
    fi
    chess_native_engine=$(realpath -e -- "$chess_native_engine")
    if [[ "$(dirname "$chess_native_engine")" != "$(realpath -e -- "$engine_directory")" ]]; then
        echo "chess-line native engine must belong to the selected engine directory" >&2
        exit 64
    fi
    chess_evidence_root=$(realpath -e -- "$chess_evidence_root")
    repository_root=$(realpath -e -- "$(dirname "$0")/../..")
    if [[ "$chess_evidence_root" == "$repository_root" || "$chess_evidence_root" == "$repository_root"/* ]]; then
        echo "chess-line evidence must be outside the source repository" >&2
        exit 64
    fi
    chess_evidence_directory=$(mktemp -d "$chess_evidence_root/chess-line-postgres.XXXXXX")
    postgres_client_library_directory=$("$pg_bindir/pg_config" --libdir)
    chess_phase=initialization
fi

"$pg_bindir/initdb" -D "$data_directory" \
    --no-locale --encoding=UTF8 --auth=trust >/dev/null
postgres_options="-k $socket_directory -p $port -c listen_addresses= -c max_prepared_transactions=4 -c laplace.perfcache_root=$perfcache_root -c extension_control_path=$control_root -c dynamic_library_path=$module_directory:$postgres_library_directory:$engine_directory"
if [[ "$mode" == "composition-measurement" ]]; then
    postgres_options="$postgres_options -c shared_buffers=1GB -c max_wal_size=8GB -c checkpoint_timeout=30min -c track_io_timing=on -c track_wal_io_timing=on"
elif [[ "$mode" == "chess-line" ]]; then
    postgres_options="$postgres_options -c fsync=on -c synchronous_commit=on -c full_page_writes=on"
else
    postgres_options="-F $postgres_options"
fi
if [[ "$mode" == "unicode-root" || "$mode" == "unicode-access-mutation" ||
      "$mode" == "source-admission" || "$mode" == "iso-639-admission" ||
      "$mode" == "cili-admission" || "$mode" == "source-admission-suite" ||
      "$mode" == "chess-line" ]]; then
    postgres_options="$postgres_options -c shared_buffers=512MB -c max_wal_size=8GB -c checkpoint_timeout=30min"
fi
if [[ "$mode" == "unicode-root" || "$mode" == "source-admission" || "$mode" == "iso-639-admission" ||
      "$mode" == "cili-admission" || "$mode" == "source-admission-suite" ||
      "$mode" == "chess-line" ]]; then
    statement_timeout_ms=${LAPLACE_POSTGRES_STATEMENT_TIMEOUT_MS:-60000}
    if [[ "$mode" == "unicode-root" ]]; then
        statement_timeout_ms=${LAPLACE_POSTGRES_UNICODE_BOOTSTRAP_TIMEOUT_MS:-300000}
    fi
    temporary_file_limit_kb=${LAPLACE_POSTGRES_TEMP_FILE_LIMIT_KB:-524288}
    if [[ ! "$statement_timeout_ms" =~ ^[1-9][0-9]*$ ||
          ! "$temporary_file_limit_kb" =~ ^[1-9][0-9]*$ ]]; then
        echo "PostgreSQL source resource limits must be positive integers" >&2
        exit 64
    fi
    postgres_options="$postgres_options -c temp_file_limit=$temporary_file_limit_kb"
fi

ASAN_OPTIONS="$server_asan_options" \
LD_PRELOAD="${sanitizer_preload}${sanitizer_preload:+${LD_PRELOAD:+:}}${LD_PRELOAD:-}" \
"$pg_bindir/pg_ctl" -D "$data_directory" -l "$server_log" \
    -o "$postgres_options" \
    -w start >/dev/null
server_started=1

psql_arguments=(
    -X
    -h "$socket_directory"
    -p "$port"
    -d postgres
    -v ON_ERROR_STOP=1
)

if [[ "$mode" == "composition-measurement" ]]; then
    probe_output=$("$native_probe")
    read_probe_value() {
        local key=$1
        local value
        value=$(awk -F= -v key="$key" '$1 == key {print $2}' <<<"$probe_output")
        if [[ ! "$value" =~ ^[0-9a-f]+$ ]]; then
            echo "native receipt probe did not emit $key" >&2
            exit 65
        fi
        printf '%s' "$value"
    }
    if [[ ! -x "$auxiliary_probe" ]]; then
        echo "composition measurement client is unavailable" >&2
        exit 84
    fi
elif [[ "$mode" == "contract" || "$mode" == "persistence-mutation" ]]; then
    probe_output=$("$native_probe")
    read_probe_value() {
        local key=$1
        local value
        value=$(awk -F= -v key="$key" '$1 == key {print $2}' <<<"$probe_output")
        if [[ ! "$value" =~ ^[0-9a-f]+$ ]]; then
            echo "native receipt probe did not emit $key" >&2
            exit 65
        fi
        printf '%s' "$value"
    }
    for key in \
        IDENTITY_RECEIPT IDENTITY_CONTEXT IDENTITY_PROGRAM IDENTITY_INPUT IDENTITY_OUTPUT \
        IDENTITY_ENTITY_0 IDENTITY_ENTITY_1 IDENTITY_ENTITY_2 \
        TRAJECTORY_RECEIPT TRAJECTORY_PROGRAM TRAJECTORY_INPUT TRAJECTORY_OUTPUT \
        TRAJECTORY_CARRIER TRAJECTORY_ENTITY \
        HIGHWAY_RECEIPT HIGHWAY_CONTEXT HIGHWAY_PROGRAM HIGHWAY_INPUT HIGHWAY_OUTPUT \
        HIGHWAY_COORDINATE_0 HIGHWAY_COORDINATE_1 \
        HIGHWAY_FINGERPRINT_0 HIGHWAY_FINGERPRINT_1 \
        PERSISTENCE_SOURCE PERSISTENCE_RECIPE \
        PERSISTENCE_ENTITY_A PERSISTENCE_ENTITY_A_WITNESS \
        PERSISTENCE_ENTITY_B PERSISTENCE_ENTITY_B_WITNESS \
        PERSISTENCE_PHYSICALITY PERSISTENCE_TRAJECTORY PERSISTENCE_OCCURRENCE \
        PERSISTENCE_PLAN_SEQUENCE \
        PERSISTENCE_FRAME_0 PERSISTENCE_FRAME_1 PERSISTENCE_FRAME_2 \
        PERSISTENCE_FRAME_3 PERSISTENCE_FRAME_4 PERSISTENCE_FRAME_5 \
        PERSISTENCE_FRAME_6 \
        PERSISTENCE_ZERO_ENTITY PERSISTENCE_ZERO_PHYSICALITY \
        PERSISTENCE_ZERO_OCCURRENCE \
        PERSISTENCE_ZERO_FRAME_0 PERSISTENCE_ZERO_FRAME_1 \
        PERSISTENCE_ZERO_FRAME_2 PERSISTENCE_ZERO_FRAME_3 \
        PERSISTENCE_CONCURRENT_PHYSICALITY PERSISTENCE_CONCURRENT_OCCURRENCE \
        PERSISTENCE_CONCURRENT_FRAME_0 PERSISTENCE_CONCURRENT_FRAME_1 \
        PERSISTENCE_CONCURRENT_FRAME_2 PERSISTENCE_CONCURRENT_FRAME_3 \
        PERSISTENCE_CONCURRENT_FRAME_4 PERSISTENCE_CONCURRENT_FRAME_5 \
        PERSISTENCE_CONCURRENT_FRAME_6 \
        PERSISTENCE_BULK_PHYSICALITY PERSISTENCE_BULK_OCCURRENCE \
        COMPOSITION_RESULT_ENTITY COMPOSITION_RESULT_PHYSICALITY \
        COMPOSITION_RESULT_TIER COMPOSITION_WORKING_SET_RECEIPT \
        COMPOSITION_PRESENCE_SEMANTIC_RECEIPT \
        COMPOSITION_PRESENCE_EXECUTION_RECEIPT \
        COMPOSITION_PRESENCE_CANDIDATE_FINGERPRINT \
        COMPOSITION_PRESENCE_DISPOSITION_FINGERPRINT \
        COMPOSITION_STREAM_FINGERPRINT COMPOSITION_ENTITY_DISPOSITIONS \
        COMPOSITION_PHYSICALITY_DISPOSITIONS; do
        shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
        psql_arguments+=(-v "$shell_name=$(read_probe_value "$key")")
    done
    for key in \
        SOURCE_PROFILE_A_ID SOURCE_PROFILE_B_ID SOURCE_PROFILE_RECEIPT \
        SOURCE_PROFILE_BOUNDARY SOURCE_PROFILE_INPUT SOURCE_PROFILE_OUTPUT \
        SOURCE_PROFILE_ISA_RECEIPT; do
        shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
        psql_arguments+=(-v "$shell_name=$(read_probe_value "$key")")
    done
    for key in \
        REFERENCE_MAPPING_ID_0 REFERENCE_MAPPING_ID_1 \
        REFERENCE_MAPPING_ID_2 \
        REFERENCE_MAPPING_PROPOSITION_0 REFERENCE_MAPPING_PROPOSITION_1 \
        REFERENCE_MAPPING_PROPOSITION_2 \
        REFERENCE_MAPPING_OCCURRENCE_0 REFERENCE_MAPPING_OCCURRENCE_1 \
        REFERENCE_MAPPING_OCCURRENCE_2 \
        REFERENCE_MAPPING_RECEIPT REFERENCE_MAPPING_BOUNDARY \
        REFERENCE_MAPPING_INPUT REFERENCE_MAPPING_OUTPUT \
        REFERENCE_MAPPING_ISA_RECEIPT; do
        shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
        psql_arguments+=(-v "$shell_name=$(read_probe_value "$key")")
    done
    for key in \
        EVIDENCE_ROOT_NODE EVIDENCE_COPY_NODE EVIDENCE_INDEPENDENT_NODE \
        EVIDENCE_ROOT_SOURCE EVIDENCE_ROOT_CONTEXT \
        EVIDENCE_COPY_SOURCE EVIDENCE_COPY_CONTEXT \
        EVIDENCE_INDEPENDENT_SOURCE EVIDENCE_INDEPENDENT_CONTEXT \
        EVIDENCE_LINEAGE_RECEIPT EVIDENCE_LINEAGE_INPUT \
        EVIDENCE_LINEAGE_OUTPUT EVIDENCE_ISA_RECEIPT; do
        shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
        psql_arguments+=(-v "$shell_name=$(read_probe_value "$key")")
    done
    for key in \
        TESTIMONY_PROFILE TESTIMONY_RECIPE TESTIMONY_TRUST \
        TESTIMONY_0_ID TESTIMONY_0_NODE TESTIMONY_0_OUTCOME \
        TESTIMONY_0_SOURCE_TYPE TESTIMONY_0_OUTCOME_TYPE \
        TESTIMONY_0_DISPOSITION TESTIMONY_0_UNCERTAINTY_NUMERATOR \
        TESTIMONY_0_UNCERTAINTY_DENOMINATOR TESTIMONY_0_SAMPLE_COUNT \
        TESTIMONY_1_ID TESTIMONY_1_NODE TESTIMONY_1_OUTCOME \
        TESTIMONY_1_SOURCE_TYPE TESTIMONY_1_OUTCOME_TYPE \
        TESTIMONY_1_DISPOSITION TESTIMONY_1_UNCERTAINTY_NUMERATOR \
        TESTIMONY_1_UNCERTAINTY_DENOMINATOR TESTIMONY_1_SAMPLE_COUNT \
        TESTIMONY_2_ID TESTIMONY_2_NODE TESTIMONY_2_OUTCOME \
        TESTIMONY_2_SOURCE_TYPE TESTIMONY_2_OUTCOME_TYPE \
        TESTIMONY_2_DISPOSITION TESTIMONY_2_UNCERTAINTY_NUMERATOR \
        TESTIMONY_2_UNCERTAINTY_DENOMINATOR TESTIMONY_2_SAMPLE_COUNT \
        TESTIMONY_RECEIPT TESTIMONY_INPUT TESTIMONY_OUTPUT \
        TESTIMONY_ISA_RECEIPT \
        WORLD_PROFILE_ID WORLD_OBSERVATION_PROFILE_ID \
        WORLD_OCCURRENCE_ID WORLD_EVIDENCE_NODE \
        WORLD_EVIDENCE_SOURCE WORLD_EVIDENCE_CONTEXT \
        WORLD_TESTIMONY_ID WORLD_TESTIMONY_TRUST WORLD_TESTIMONY_OUTCOME; do
        shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
        psql_arguments+=(-v "$shell_name=$(read_probe_value "$key")")
    done
    if [[ "$mode" == "persistence-mutation" ]]; then
        if [[ -z "${LAPLACE_MUTANT_MODULE:-}" || ! -f "$LAPLACE_MUTANT_MODULE" ]]; then
            echo "persistence mutant module is missing" >&2
            exit 66
        fi
        psql_arguments+=(-v "persistence_mutant_module=$LAPLACE_MUTANT_MODULE")
    fi
    bulk_stream=$(read_probe_value PERSISTENCE_BULK_STREAM)
    variable_file="$test_root/native-variables.sql"
    umask 077
    printf "\\set persistence_bulk_stream '%s'\n" "$bulk_stream" >"$variable_file"
elif [[ "$mode" == "standing" || "$mode" == "standing-mutation" ||
        "$mode" == "standing-admission-mutation" ]]; then
    probe_output=$(LD_LIBRARY_PATH="$engine_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$native_probe")
    while IFS='=' read -r key value; do
        if [[ ! "$key" =~ ^STANDING_[A-Z0-9_]+$ ||
              ! "$value" =~ ^[0-9a-f.-]+$ ]]; then
            echo "native standing probe emitted an invalid value: $key" >&2
            exit 88
        fi
        shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
        psql_arguments+=(-v "$shell_name=$value")
    done <<<"$probe_output"
    if [[ "$mode" == "standing-mutation" ||
          "$mode" == "standing-admission-mutation" ]]; then
        if [[ -z "${LAPLACE_MUTANT_MODULE:-}" || ! -f "$LAPLACE_MUTANT_MODULE" ]]; then
            echo "standing replay mutant module is missing" >&2
            exit 66
        fi
        psql_arguments+=(-v "standing_mutant_module=$LAPLACE_MUTANT_MODULE")
    fi
elif [[ "$mode" == "stock-catalog" ]]; then
    probe_output=$(LD_LIBRARY_PATH="$engine_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$native_probe")
    while IFS='=' read -r key value; do
        if [[ ! "$key" =~ ^STOCK_[A-Z0-9_]+$ ||
              ! "$value" =~ ^[0-9a-f]+$ ]]; then
            echo "native stock recipe probe emitted an invalid value: $key" >&2
            exit 89
        fi
        shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
        psql_arguments+=(-v "$shell_name=$value")
    done <<<"$probe_output"
elif [[ "$mode" == "machine-exception" ||
        "$mode" == "machine-exception-mutation" ]]; then
    probe_output=$(LD_LIBRARY_PATH="$engine_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$native_probe")
    expected_hex=$(awk -F= \
        '$1 == "MACHINE_EXCEPTION_EXPECTED_HEX" {print $2}' <<<"$probe_output")
    if [[ ! "$expected_hex" =~ ^[0-9a-f]+$ ]]; then
        echo "native machine-exception probe emitted an invalid registry" >&2
        exit 90
    fi
    psql_arguments+=(-v "machine_exception_expected_hex=$expected_hex")
    if [[ "$mode" == "machine-exception-mutation" ]]; then
        if [[ -z "${LAPLACE_MUTANT_MODULE:-}" ||
              ! -f "$LAPLACE_MUTANT_MODULE" ]]; then
            echo "machine-exception mutant module is missing" >&2
            exit 66
        fi
        psql_arguments+=(-v "machine_exception_mutant_module=$LAPLACE_MUTANT_MODULE")
    fi
elif [[ "$mode" == "perfcache-mutation" ]]; then
    if [[ -z "${LAPLACE_MUTANT_MODULE:-}" || ! -f "$LAPLACE_MUTANT_MODULE" ]]; then
        echo "perfcache mutant module is missing" >&2
        exit 66
    fi
    psql_arguments+=(-v "perfcache_mutant_module=$LAPLACE_MUTANT_MODULE")
elif [[ "$mode" == "unicode-root" || "$mode" == "unicode-access-mutation" ||
      "$mode" == "source-admission" || "$mode" == "iso-639-admission" ||
      "$mode" == "cili-admission" || "$mode" == "source-admission-suite" ||
      "$mode" == "chess-line" ]]; then
    unicode_source_root=${LAPLACE_UNICODE_SOURCE_ROOT:-}
    if [[ -z "$unicode_source_root" || ! -d "$unicode_source_root" ]]; then
        echo "verified Unicode source root is unavailable: $unicode_source_root" >&2
        exit 77
    fi
    unicode_spool_directory="$test_root/unicode-spool"
    unicode_tier0_path="$perfcache_root/unicode-tier0.bin"
    unicode_reverse_path="$perfcache_root/unicode-identity-reverse.bin"
    mkdir -p -- "$unicode_spool_directory"
    psql_arguments+=(
        -v "unicode_source_root=$unicode_source_root"
        -v "unicode_spool_directory=$unicode_spool_directory"
        -v "unicode_tier0_path=$unicode_tier0_path"
        -v "unicode_reverse_path=$unicode_reverse_path")
    if [[ "$mode" == "source-admission-suite" ]]; then
        source_probe=${LAPLACE_SOURCE_ADMISSION_PROBE:-}
        iso_probe=${LAPLACE_ISO_639_PROFILE_PROBE:-}
        cili_probe=${LAPLACE_CILI_PROFILE_PROBE:-}
        for probe in "$source_probe" "$iso_probe" "$cili_probe"; do
            if [[ -z "$probe" || ! -x "$probe" ]]; then
                echo "source-admission suite probe is unavailable: $probe" >&2
                exit 85
            fi
        done

        probe_output=$("$source_probe")
        for key in TABULAR_ARTIFACT_GRAPH TABULAR_ARCHIVE_ID TABULAR_TEXT_ID; do
            value=$(awk -F= -v key="$key" '$1 == key {print $2}' <<<"$probe_output")
            if [[ ! "$value" =~ ^[0-9a-f]+$ ]]; then
                echo "native source probe did not emit $key" >&2
                exit 85
            fi
            shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
            psql_arguments+=(-v "$shell_name=$value")
        done

        iso_source_root=${LAPLACE_ISO_639_SOURCE_ROOT:-/vault/Data/ISO639}
        cili_source_root=${LAPLACE_CILI_SOURCE_ROOT:-}
        if [[ ! -d "$iso_source_root" ]]; then
            echo "verified ISO 639 source root is unavailable: $iso_source_root" >&2
            exit 77
        fi
        if [[ -z "$cili_source_root" || ! -d "$cili_source_root" ]]; then
            echo "verified CILI source root is unavailable: $cili_source_root" >&2
            exit 77
        fi

        probe_output=$(LD_LIBRARY_PATH="$engine_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
            "$iso_probe" "$iso_source_root" "$unicode_source_root")
        while IFS='=' read -r key value; do
            if [[ "$key" != ISO_* || ! "$value" =~ ^[0-9a-f]+$ ]]; then
                echo "native ISO 639 profile probe emitted an invalid value: $key" >&2
                exit 86
            fi
            shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
            psql_arguments+=(-v "$shell_name=$value")
        done <<<"$probe_output"
        psql_arguments+=(-v "iso_source_root=$iso_source_root")

        probe_output=$(LD_LIBRARY_PATH="$engine_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
            "$cili_probe" "$cili_source_root" "$unicode_source_root")
        while IFS='=' read -r key value; do
            if [[ "$key" != CILI_* || ! "$value" =~ ^[0-9a-f]+$ ]]; then
                echo "native CILI profile probe emitted an invalid value: $key" >&2
                exit 87
            fi
            shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
            psql_arguments+=(-v "$shell_name=$value")
        done <<<"$probe_output"
        psql_arguments+=(-v "cili_source_root=$cili_source_root")
    fi
    if [[ "$mode" == "iso-639-admission" ]]; then
        iso_source_root=${LAPLACE_ISO_639_SOURCE_ROOT:-/vault/Data/ISO639}
        if [[ ! -d "$iso_source_root" ]]; then
            echo "verified ISO 639 source root is unavailable: $iso_source_root" >&2
            exit 77
        fi
        probe_output=$(LD_LIBRARY_PATH="$engine_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
            "$native_probe" "$iso_source_root" "$unicode_source_root")
        while IFS='=' read -r key value; do
            if [[ "$key" != ISO_* || ! "$value" =~ ^[0-9a-f]+$ ]]; then
                echo "native ISO 639 profile probe emitted an invalid value: $key" >&2
                exit 86
            fi
            shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
            psql_arguments+=(-v "$shell_name=$value")
        done <<<"$probe_output"
        psql_arguments+=(-v "iso_source_root=$iso_source_root")
    fi
    if [[ "$mode" == "cili-admission" ]]; then
        cili_source_root=${LAPLACE_CILI_SOURCE_ROOT:-}
        if [[ -z "$cili_source_root" || ! -d "$cili_source_root" ]]; then
            echo "verified CILI source root is unavailable: $cili_source_root" >&2
            exit 77
        fi
        probe_output=$(LD_LIBRARY_PATH="$engine_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
            "$native_probe" "$cili_source_root" "$unicode_source_root")
        while IFS='=' read -r key value; do
            if [[ "$key" != CILI_* || ! "$value" =~ ^[0-9a-f]+$ ]]; then
                echo "native CILI profile probe emitted an invalid value: $key" >&2
                exit 87
            fi
            shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
            psql_arguments+=(-v "$shell_name=$value")
        done <<<"$probe_output"
        psql_arguments+=(-v "cili_source_root=$cili_source_root")
    fi
    if [[ "$mode" == "source-admission" || "$mode" == "chess-line" ]]; then
        if [[ "$mode" == "chess-line" ]]; then
            chess_phase=source-authority
            probe_output=$(LD_LIBRARY_PATH="$engine_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
                timeout 30 "$native_probe")
        else
            probe_output=$("$native_probe")
        fi
        for key in TABULAR_ARTIFACT_GRAPH TABULAR_ARCHIVE_ID TABULAR_TEXT_ID; do
            value=$(awk -F= -v key="$key" '$1 == key {print $2}' <<<"$probe_output")
            if [[ ! "$value" =~ ^[0-9a-f]+$ ]]; then
                echo "native source probe did not emit $key" >&2
                exit 85
            fi
            shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
            psql_arguments+=(-v "$shell_name=$value")
        done
    fi
    if [[ "$mode" == "unicode-access-mutation" ]]; then
        if [[ -z "${LAPLACE_MUTANT_MODULE:-}" || ! -f "$LAPLACE_MUTANT_MODULE" ]]; then
            echo "Unicode access mutant module is missing" >&2
            exit 66
        fi
        psql_arguments+=(-v "unicode_access_mutant_module=$LAPLACE_MUTANT_MODULE")
    fi
elif [[ "$mode" != "mutation" ]]; then
    echo "unknown PostgreSQL test mode: $mode" >&2
    exit 64
fi

if [[ "$mode" == "unicode-root" ]]; then
    firmware_compiler=${LAPLACE_COGNITION_FIRMWARE_COMPILER:-}
    if [[ -z "$firmware_compiler" || ! -x "$firmware_compiler" ]]; then
        echo "real product cognition firmware compiler is unavailable" >&2
        exit 88
    fi
    firmware_document="$test_root/product-cognition-firmware.json"
    firmware_values="$test_root/product-cognition-firmware-values"
    LD_LIBRARY_PATH="$engine_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        timeout 30 "$firmware_compiler" --relation constituent >"$firmware_document"
    python3 - "$firmware_document" "$firmware_values" <<'PY'
import json
from pathlib import Path
import re
import sys

raw = Path(sys.argv[1]).read_bytes()
if len(raw) > 1048576:
    raise SystemExit("product firmware compiler document exceeds its bound")
value = json.loads(raw)
if (not isinstance(value, dict)
        or value.get("schema") != "laplace.cognition-firmware-image/v1"
        or value.get("program") != "relation-chain-exact-witnessed-output"
        or value.get("mode") != "explicit"
        or value.get("relations") != ["constituent"]
        or value.get("step_count") != 2
        or not isinstance(value.get("program_id"), str)
        or re.fullmatch(r"[0-9a-f]{64}", value["program_id"]) is None
        or not isinstance(value.get("image_hex"), str)
        or re.fullmatch(r"(?:[0-9a-f]{2})+", value["image_hex"]) is None
        or type(value.get("image_bytes")) is not int
        or value["image_bytes"] != len(value["image_hex"]) // 2):
    raise SystemExit("product firmware compiler did not emit the selected native program")
Path(sys.argv[2]).write_text(value["program_id"] + "\n" + value["image_hex"] + "\n")
PY
    mapfile -t selected_firmware <"$firmware_values"
    [[ "${#selected_firmware[@]}" == 2 ]] || exit 88
    psql_arguments+=(
        -v "product_cognition_program_id=${selected_firmware[0]}"
        -v "product_cognition_image=${selected_firmware[1]}")
fi

if [[ "$mode" == "contract" || "$mode" == "perfcache-mutation" ]]; then
    perfcache_probe_output=$("$auxiliary_probe" "$perfcache_root")
    declare -A perfcache_manifests
    for index in 1 2 3 4 5 6 7; do
        manifest=$(awk -F= -v key="PERFCACHE_MANIFEST_$index" \
            '$1 == key {print $2}' <<<"$perfcache_probe_output")
        if [[ ! "$manifest" =~ ^[0-9a-f]+$ ]]; then
            echo "perfcache probe did not emit manifest $index" >&2
            exit 79
        fi
        perfcache_manifests[$index]=$manifest
        psql_arguments+=(-v "perfcache_manifest_$index=$manifest")
    done
fi

if [[ "$mode" == "composition-measurement" ]]; then
    "$auxiliary_probe" \
        "$socket_directory" "$port" "$sql_file" \
        "$(read_probe_value PERSISTENCE_ENTITY_A)" \
        "$(read_probe_value PERSISTENCE_ENTITY_A_WITNESS)" \
        "$(read_probe_value PERSISTENCE_ENTITY_B)" \
        "$(read_probe_value PERSISTENCE_ENTITY_B_WITNESS)" \
        "${LAPLACE_COMPOSITION_MEASUREMENT_SAMPLES:-5}"
    exit 0
fi

if [[ "$mode" == "source-admission" || "$mode" == "iso-639-admission" ||
      "$mode" == "cili-admission" || "$mode" == "source-admission-suite" ||
      "$mode" == "chess-line" ]]; then
    unicode_bootstrap_timeout_ms=${LAPLACE_POSTGRES_UNICODE_BOOTSTRAP_TIMEOUT_MS:-300000}
    if [[ ! "$unicode_bootstrap_timeout_ms" =~ ^[1-9][0-9]*$ ]]; then
        echo "PostgreSQL Unicode bootstrap timeout must be a positive integer" >&2
        exit 64
    fi
    postmaster_pid=$(head -n 1 -- "$data_directory/postmaster.pid")
    if [[ "$mode" == "chess-line" ]]; then chess_phase=unicode-bootstrap; fi
    unicode_bootstrap_command=(
        "$pg_bindir/psql" "${psql_arguments[@]}"
        -c "SET statement_timeout = '$unicode_bootstrap_timeout_ms ms'"
        -f "$(dirname "$sql_file")/unicode_root_contract.sql")
    python3 "$(dirname "$0")/../../tools/tests/postgres_resource_guard.py" \
        --data-directory "$data_directory" \
        --workspace-directory "$test_root" \
        --postmaster-pid "$postmaster_pid" \
        --max-wall-seconds "${LAPLACE_POSTGRES_UNICODE_MAX_WALL_SECONDS:-300}" \
        --max-data-bytes "${LAPLACE_POSTGRES_UNICODE_MAX_DATA_BYTES:-8589934592}" \
        --max-wal-bytes "${LAPLACE_POSTGRES_UNICODE_MAX_WAL_BYTES:-8589934592}" \
        --max-workspace-bytes "${LAPLACE_POSTGRES_UNICODE_MAX_WORKSPACE_BYTES:-12884901888}" \
        --max-rss-bytes "${LAPLACE_POSTGRES_MAX_RSS_BYTES:-12884901888}" \
        --sample-seconds 1 \
        --receipt "$test_root/unicode-resource-guard.json" \
        -- "${unicode_bootstrap_command[@]}"
    psql_arguments+=(-v source_skip_unicode=1)
fi

if [[ "$mode" == "chess-line" ]]; then chess_phase=source-setup; fi
psql_command=("$pg_bindir/psql" "${psql_arguments[@]}")
if [[ "$mode" == "unicode-root" || "$mode" == "source-admission" || "$mode" == "iso-639-admission" ||
      "$mode" == "cili-admission" || "$mode" == "source-admission-suite" ||
      "$mode" == "chess-line" ]]; then
    psql_command+=(-c "SET statement_timeout = '$statement_timeout_ms ms'")
fi
if [[ -n "${variable_file:-}" ]]; then
    psql_command+=(-f "$variable_file")
fi
psql_command+=(-f "$sql_file")
if [[ "$mode" == "unicode-root" ]]; then
    # Reuse the actual activated private Unicode/Highway backend and authority.
    psql_command+=(-f "$(dirname "$sql_file")/product_cognition_retained_prompt_contract.sql")
fi
if [[ "$mode" == "source-admission" ]]; then
    # This companion uses the admitted Unicode context, then reconnects to prove
    # cold descriptor readback. Keep it last because pg_temp state is per backend.
    psql_command+=(-f "$(dirname "$sql_file")/physicality_entity_contract.sql")
fi

if [[ "$mode" == "unicode-root" || "$mode" == "source-admission" || "$mode" == "iso-639-admission" ||
      "$mode" == "cili-admission" || "$mode" == "source-admission-suite" ||
      "$mode" == "chess-line" ]]; then
    source_max_wall_seconds=${LAPLACE_POSTGRES_MAX_WALL_SECONDS:-60}
    source_max_data_bytes=${LAPLACE_POSTGRES_MAX_DATA_BYTES:-1073741824}
    source_max_wal_bytes=${LAPLACE_POSTGRES_MAX_WAL_BYTES:-536870912}
    source_max_workspace_bytes=${LAPLACE_POSTGRES_MAX_WORKSPACE_BYTES:-2147483648}
    if [[ "$mode" == "unicode-root" ]]; then
        postmaster_pid=$(head -n 1 -- "$data_directory/postmaster.pid")
        source_max_wall_seconds=${LAPLACE_POSTGRES_UNICODE_MAX_WALL_SECONDS:-300}
        source_max_data_bytes=${LAPLACE_POSTGRES_UNICODE_MAX_DATA_BYTES:-8589934592}
        source_max_wal_bytes=${LAPLACE_POSTGRES_UNICODE_MAX_WAL_BYTES:-8589934592}
        source_max_workspace_bytes=${LAPLACE_POSTGRES_UNICODE_MAX_WORKSPACE_BYTES:-12884901888}
    fi
    if [[ "$mode" == "source-admission-suite" ]]; then
        source_max_wall_seconds=${LAPLACE_POSTGRES_SOURCE_SUITE_MAX_WALL_SECONDS:-180}
        source_max_data_bytes=${LAPLACE_POSTGRES_SOURCE_SUITE_MAX_DATA_BYTES:-3221225472}
        source_max_wal_bytes=${LAPLACE_POSTGRES_SOURCE_SUITE_MAX_WAL_BYTES:-1610612736}
        source_max_workspace_bytes=${LAPLACE_POSTGRES_SOURCE_SUITE_MAX_WORKSPACE_BYTES:-6442450944}
    fi
    python3 "$(dirname "$0")/../../tools/tests/postgres_resource_guard.py" \
        --data-directory "$data_directory" \
        --workspace-directory "$test_root" \
        --postmaster-pid "$postmaster_pid" \
        --max-wall-seconds "$source_max_wall_seconds" \
        --max-data-bytes "$source_max_data_bytes" \
        --max-wal-bytes "$source_max_wal_bytes" \
        --max-workspace-bytes "$source_max_workspace_bytes" \
        --max-rss-bytes "${LAPLACE_POSTGRES_MAX_RSS_BYTES:-12884901888}" \
        --sample-seconds 1 \
        --receipt "$test_root/resource-guard.json" \
        -- "${psql_command[@]}"
else
    "${psql_command[@]}"
fi

if [[ "$mode" == "unicode-root" || "$mode" == "unicode-access-mutation" ||
      "$mode" == "source-admission" || "$mode" == "iso-639-admission" ||
      "$mode" == "cili-admission" || "$mode" == "source-admission-suite" ||
      "$mode" == "chess-line" ]]; then
    if [[ ! -f "$unicode_tier0_path" ]]; then
        echo "Unicode Tier-0 artifact was not published" >&2
        exit 80
    fi
    if [[ ! -f "$unicode_reverse_path" ]]; then
        echo "Unicode identity reverse artifact was not published" >&2
        exit 82
    fi
    unicode_tier0_bytes=$(stat -c '%s' -- "$unicode_tier0_path")
    if [[ "$unicode_tier0_bytes" != 762586574 ]]; then
        echo "Unicode Tier-0 artifact has unexpected size: $unicode_tier0_bytes" >&2
        exit 81
    fi
    unicode_reverse_bytes=$(stat -c '%s' -- "$unicode_reverse_path")
    if [[ "$unicode_reverse_bytes" != 117440896 ]]; then
        echo "Unicode identity reverse artifact has unexpected size: $unicode_reverse_bytes" >&2
        exit 83
    fi
fi

if [[ "$mode" == "chess-line" ]]; then
    chess_phase=native-line-replay
    # The native probe performs actual commit, full-body readback, corruption
    # controls and fresh-backend replay; it writes its own success/failure state.
    python3 "$(dirname "$0")/../../tools/tests/postgres_resource_guard.py" \
        --data-directory "$data_directory" \
        --workspace-directory "$test_root" \
        --postmaster-pid "$postmaster_pid" \
        --max-wall-seconds "${LAPLACE_POSTGRES_CHESS_LINE_MAX_WALL_SECONDS:-120}" \
        --max-data-bytes "${LAPLACE_POSTGRES_MAX_DATA_BYTES:-1073741824}" \
        --max-wal-bytes "${LAPLACE_POSTGRES_MAX_WAL_BYTES:-536870912}" \
        --max-workspace-bytes "${LAPLACE_POSTGRES_MAX_WORKSPACE_BYTES:-2147483648}" \
        --max-rss-bytes "${LAPLACE_POSTGRES_MAX_RSS_BYTES:-12884901888}" \
        --sample-seconds 1 \
        --receipt "$test_root/chess-line-resource-guard.json" \
        -- env \
            ASAN_OPTIONS="$server_asan_options" \
            LD_PRELOAD="${sanitizer_preload}${sanitizer_preload:+${LD_PRELOAD:+:}}${LD_PRELOAD:-}" \
            LD_LIBRARY_PATH="$engine_directory:$postgres_client_library_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
            "$auxiliary_probe" "$socket_directory" "$port" \
            "$chess_manifest" "$chess_pgn" "$chess_native_engine" \
            "$test_root/chess-line-receipt.json" "$test_root/chess-line-observations.json"
    chess_phase=evidence-validation
    python3 - "$test_root/chess-line-receipt.json" "$test_root/chess-line-observations.json" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

paths = [Path(value) for value in sys.argv[1:]]
for path in paths:
    if path.is_symlink() or not path.is_file() or not 0 < path.stat().st_size <= 32 * 1024 * 1024:
        raise SystemExit("native chess probe did not retain bounded regular evidence")
receipt_bytes, observations_bytes = (path.read_bytes() for path in paths)
receipt = json.loads(receipt_bytes)
observations = json.loads(observations_bytes)
if (not isinstance(receipt, dict)
        or receipt.get("schema") != "laplace.chess-line-postgres-acceptance/v1"
        or receipt.get("status") != "passed"
        or any(receipt.get(field) is not True for field in (
            "native_trace_execution", "active_unicode_composition_execution",
            "postgresql_line_admission", "fresh_backend_verified",
            "exact_warm_cold_result", "caller_rollback_verified",
            "backend_engine_mappings_verified", "fsync", "synchronous_commit", "full_page_writes"))
        or receipt.get("full_pgn_parser_in_this_probe") is not False
        or receipt.get("raw_pgn_source_profile_admitted") is not False
        or type(receipt.get("playing_entities_admitted")) is not int
        or receipt["playing_entities_admitted"] != 0
        or receipt.get("recorded_game_rate_measured") is not False
        or receipt.get("observations_sha256") != hashlib.sha256(observations_bytes).hexdigest()
        or not isinstance(observations, dict)
        or observations.get("schema") != "laplace.chess-line-source-observations/v1"
        or type(observations.get("source_occurrences")) is not int
        or observations["source_occurrences"] != 1):
    raise SystemExit("native chess acceptance evidence is incomplete or mismatched")
PY
    chess_phase=completed
fi

if [[ "$mode" == "contract" ]]; then
    perfcache_contract_sql="$(dirname "$sql_file")/perfcache_epoch_contract.sql"
    perfcache_cold_lookup_sql="$(dirname "$sql_file")/perfcache_cold_lookup.sql"
    perfcache_hold_sql="$(dirname "$sql_file")/perfcache_concurrency_hold.sql"
    perfcache_activate_sql="$(dirname "$sql_file")/perfcache_concurrency_activate.sql"
    perfcache_verify_sql="$(dirname "$sql_file")/perfcache_concurrency_verify.sql"
    perfcache_prepare_sql="$(dirname "$sql_file")/perfcache_prepare_rejected.sql"
    perfcache_recreate_sql="$(dirname "$sql_file")/perfcache_recreate.sql"
    perfcache_competing_activate_sql="$(dirname "$sql_file")/perfcache_competing_activate.sql"
    perfcache_competing_verify_sql="$(dirname "$sql_file")/perfcache_competing_verify.sql"
    postmaster_started=$(
        "$pg_bindir/psql" "${psql_arguments[@]}" -Atqc \
            'SELECT pg_postmaster_start_time()')
    "$pg_bindir/psql" "${psql_arguments[@]}" -f "$perfcache_contract_sql"
    "$pg_bindir/psql" "${psql_arguments[@]}" -f "$perfcache_cold_lookup_sql"
    "$pg_bindir/psql" "${psql_arguments[@]}" -f "$perfcache_hold_sql" \
        >"$test_root/perfcache-reader.log" 2>&1 &
    perfcache_reader_pid=$!
    reader_observed=0
    for _ in {1..100}; do
        active_readers=$(
            "$pg_bindir/psql" "${psql_arguments[@]}" -Atqc \
                'SELECT laplace._test_perfcache_metric(2)')
        if [[ "$active_readers" == 1 ]]; then
            reader_observed=1
            break
        fi
        sleep 0.02
    done
    if [[ $reader_observed -ne 1 ]]; then
        cat "$test_root/perfcache-reader.log" >&2
        echo "concurrent perfcache reader did not acquire its epoch pin" >&2
        exit 68
    fi
    "$pg_bindir/psql" "${psql_arguments[@]}" -f "$perfcache_activate_sql"
    exact_worker_epoch=$(
        "$pg_bindir/psql" "${psql_arguments[@]}" -Atqc \
            "SELECT encode(laplace._test_perfcache_hold(decode(repeat('22',16),'hex'),decode(repeat('b2',32),'hex'),0),'hex')")
    if [[ "$exact_worker_epoch" != "$(printf '22%.0s' {1..16})$(printf 'b2%.0s' {1..32})" ]]; then
        echo "worker did not retain the leader's exact retired epoch" >&2
        exit 78
    fi
    if ! wait "$perfcache_reader_pid"; then
        cat "$test_root/perfcache-reader.log" >&2
        exit 69
    fi
    "$pg_bindir/psql" "${psql_arguments[@]}" -f "$perfcache_verify_sql"

    "$pg_bindir/psql" "${psql_arguments[@]}" -c \
        "SELECT laplace._test_perfcache_hold(decode(repeat('33',16),'hex'),decode(repeat('c3',32),'hex'),60000)" \
        >"$test_root/perfcache-terminated-reader.log" 2>&1 &
    terminated_reader_pid=$!
    terminated_reader_observed=0
    for _ in {1..100}; do
        active_readers=$(
            "$pg_bindir/psql" "${psql_arguments[@]}" -Atqc \
                'SELECT laplace._test_perfcache_metric(2)')
        if [[ "$active_readers" == 1 ]]; then
            terminated_reader_observed=1
            break
        fi
        sleep 0.02
    done
    if [[ $terminated_reader_observed -ne 1 ]]; then
        cat "$test_root/perfcache-terminated-reader.log" >&2
        echo "terminable perfcache reader did not acquire its epoch pin" >&2
        exit 71
    fi
    terminated_backend=$(
        "$pg_bindir/psql" "${psql_arguments[@]}" -Atqc \
            "SELECT pid FROM pg_stat_activity WHERE pid <> pg_backend_pid() AND query LIKE '%_test_perfcache_hold%' ORDER BY backend_start DESC LIMIT 1")
    if [[ ! "$terminated_backend" =~ ^[0-9]+$ ]]; then
        echo "cannot identify the perfcache reader backend" >&2
        exit 72
    fi
    "$pg_bindir/psql" "${psql_arguments[@]}" -Atqc \
        "SELECT pg_terminate_backend($terminated_backend)" >/dev/null
    if wait "$terminated_reader_pid"; then
        echo "terminated perfcache reader unexpectedly succeeded" >&2
        exit 73
    fi
    if [[ $(
        "$pg_bindir/psql" "${psql_arguments[@]}" -Atqc \
            'SELECT laplace._test_perfcache_metric(2)') != 0 ]]; then
        cat "$test_root/perfcache-terminated-reader.log" >&2
        echo "backend termination leaked a perfcache generation pin" >&2
        exit 74
    fi

    if "$pg_bindir/psql" "${psql_arguments[@]}" -f "$perfcache_prepare_sql" \
        >"$test_root/perfcache-prepare.log" 2>&1; then
        echo "pending perfcache activation entered a prepared transaction" >&2
        exit 75
    fi
    if ! grep -Fq \
        'prepared transactions cannot carry a pending Laplace perfcache activation' \
        "$test_root/perfcache-prepare.log"; then
        cat "$test_root/perfcache-prepare.log" >&2
        echo "prepared-transaction rejection failed for an unrelated reason" >&2
        exit 76
    fi
    prepare_cleanup_state=$(
        "$pg_bindir/psql" "${psql_arguments[@]}" -Atqc \
            "SELECT laplace._test_perfcache_metric(1), laplace._test_perfcache_metric(5), (SELECT count(*) FROM pg_prepared_xacts WHERE gid = 'laplace-perfcache-must-not-prepare')")
    if [[ "$prepare_cleanup_state" != "3|0|0" ]]; then
        cat "$test_root/perfcache-prepare.log" >&2
        echo "rejected prepared transaction changed or retained perfcache state" >&2
        exit 77
    fi

    if [[ "$postmaster_started" != $(
        "$pg_bindir/psql" "${psql_arguments[@]}" -Atqc \
            'SELECT pg_postmaster_start_time()') ]]; then
        echo "perfcache epoch handoff restarted PostgreSQL" >&2
        exit 70
    fi

    concurrency_sql="$(dirname "$sql_file")/persistence_concurrency_call.sql"
    concurrency_verify_sql="$(dirname "$sql_file")/persistence_concurrency_verify.sql"
    deposit_receipts_before_concurrency=$(
        "$pg_bindir/psql" "${psql_arguments[@]}" -Atqc \
            'SELECT count(*) FROM laplace.canonical_deposit_receipt')
    if [[ ! "$deposit_receipts_before_concurrency" =~ ^[0-9]+$ ]]; then
        echo "cannot establish persistence receipt baseline before concurrent deposit" >&2
        exit 84
    fi
    concurrency_pids=()
    for worker in 1 2; do
        "$pg_bindir/psql" "${psql_arguments[@]}" -f "$concurrency_sql" \
            >"$test_root/concurrency-$worker.log" 2>&1 &
        concurrency_pids+=("$!")
    done
    concurrency_failed=0
    for pid in "${concurrency_pids[@]}"; do
        if ! wait "$pid"; then
            concurrency_failed=1
        fi
    done
    if [[ $concurrency_failed -ne 0 ]]; then
        sed -n '1,240p' "$test_root"/concurrency-*.log >&2
        exit 67
    fi
    "$pg_bindir/psql" "${psql_arguments[@]}" \
        -v "persistence_expected_receipt_count=$((deposit_receipts_before_concurrency + 1))" \
        -f "$concurrency_verify_sql"
    "$pg_bindir/psql" "${psql_arguments[@]}" -f "$perfcache_recreate_sql"

    competing_pids=()
    for generation in 2 7; do
        "$pg_bindir/psql" "${psql_arguments[@]}" \
            -v "candidate_manifest=${perfcache_manifests[$generation]}" \
            -f "$perfcache_competing_activate_sql" \
            >"$test_root/perfcache-competing-$generation.log" 2>&1 &
        competing_pids+=("$!")
    done
    competing_successes=0
    for pid in "${competing_pids[@]}"; do
        if wait "$pid"; then
            competing_successes=$((competing_successes + 1))
        fi
    done
    if [[ $competing_successes -ne 1 ]]; then
        sed -n '1,160p' "$test_root"/perfcache-competing-*.log >&2
        echo "competing perfcache activators did not produce exactly one winner" >&2
        exit 81
    fi
    "$pg_bindir/psql" "${psql_arguments[@]}" -f "$perfcache_competing_verify_sql"
fi
