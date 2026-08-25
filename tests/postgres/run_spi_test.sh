#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 8 ]]; then
    echo "usage: $0 MODE PG-BINDIR CONTROL-ROOT MODULE-DIRECTORY ENGINE-DIRECTORY NATIVE-PROBE SQL-FILE SANITIZER-PRELOAD" >&2
    exit 64
fi

mode=$1
pg_bindir=$2
postgres_library_directory=$("$pg_bindir/pg_config" --pkglibdir)
control_root=$3
module_directory=$4
engine_directory=$5
native_probe=$6
sql_file=$7
sanitizer_preload=$8
temporary_parent=${RUNNER_TEMP:-${TMPDIR:-/tmp}}
test_root=$(mktemp -d "$temporary_parent/laplace-postgres-test.XXXXXX")
data_directory="$test_root/data"
socket_directory=$(mktemp -d /tmp/lp-pg.XXXXXX)
server_log="$test_root/postgres.log"
port=55432
server_started=0
server_asan_options=${ASAN_OPTIONS:-}
if [[ -n "$sanitizer_preload" ]]; then
    server_asan_options="${server_asan_options}${server_asan_options:+:}detect_leaks=0"
fi

cleanup() {
    exit_code=$?
    if [[ $server_started -eq 1 ]]; then
        if ! "$pg_bindir/pg_ctl" -D "$data_directory" -m fast -t 5 -w stop \
            >/dev/null 2>&1; then
            "$pg_bindir/pg_ctl" -D "$data_directory" -m immediate -t 5 -w stop \
                >/dev/null 2>&1 || true
        fi
    fi
    if [[ "$socket_directory" == /tmp/lp-pg.* ]]; then
        rmdir -- "$socket_directory" 2>/dev/null || true
    fi
    if [[ $exit_code -eq 0 && "$test_root" == "$temporary_parent"/laplace-postgres-test.* ]]; then
        rm -rf -- "$test_root"
    else
        echo "PostgreSQL test evidence retained at $test_root" >&2
    fi
    exit "$exit_code"
}
trap cleanup EXIT

"$pg_bindir/initdb" -D "$data_directory" \
    --no-locale --encoding=UTF8 --auth=trust >/dev/null
ASAN_OPTIONS="$server_asan_options" \
LD_PRELOAD="${sanitizer_preload}${sanitizer_preload:+${LD_PRELOAD:+:}}${LD_PRELOAD:-}" \
"$pg_bindir/pg_ctl" -D "$data_directory" -l "$server_log" \
    -o "-F -k $socket_directory -p $port -c listen_addresses= -c extension_control_path=$control_root -c dynamic_library_path=$postgres_library_directory:$module_directory:$engine_directory" \
    -w start >/dev/null
server_started=1

psql_arguments=(
    -X
    -h "$socket_directory"
    -p "$port"
    -d postgres
    -v ON_ERROR_STOP=1
)

if [[ "$mode" == "contract" || "$mode" == "persistence-mutation" ]]; then
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
        PERSISTENCE_BULK_PHYSICALITY PERSISTENCE_BULK_OCCURRENCE; do
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
elif [[ "$mode" != "mutation" ]]; then
    echo "unknown PostgreSQL test mode: $mode" >&2
    exit 64
fi

if [[ -n "${variable_file:-}" ]]; then
    "$pg_bindir/psql" "${psql_arguments[@]}" -f "$variable_file" -f "$sql_file"
else
    "$pg_bindir/psql" "${psql_arguments[@]}" -f "$sql_file"
fi

if [[ "$mode" == "contract" ]]; then
    concurrency_sql="$(dirname "$sql_file")/persistence_concurrency_call.sql"
    concurrency_verify_sql="$(dirname "$sql_file")/persistence_concurrency_verify.sql"
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
    "$pg_bindir/psql" "${psql_arguments[@]}" -f "$concurrency_verify_sql"
fi
