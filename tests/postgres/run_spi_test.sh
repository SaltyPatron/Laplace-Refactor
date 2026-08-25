#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 7 ]]; then
    echo "usage: $0 MODE PG-BINDIR CONTROL-ROOT MODULE-DIRECTORY ENGINE-DIRECTORY NATIVE-PROBE SQL-FILE" >&2
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
temporary_parent=${RUNNER_TEMP:-${TMPDIR:-/tmp}}
test_root=$(mktemp -d "$temporary_parent/laplace-postgres-test.XXXXXX")
data_directory="$test_root/data"
socket_directory="$test_root/socket"
server_log="$test_root/postgres.log"
port=55432
server_started=0

cleanup() {
    exit_code=$?
    if [[ $server_started -eq 1 ]]; then
        "$pg_bindir/pg_ctl" -D "$data_directory" -m fast -w stop >/dev/null || true
    fi
    if [[ $exit_code -eq 0 && "$test_root" == "$temporary_parent"/laplace-postgres-test.* ]]; then
        rm -rf -- "$test_root"
    else
        echo "PostgreSQL test evidence retained at $test_root" >&2
    fi
    exit "$exit_code"
}
trap cleanup EXIT

mkdir -p "$socket_directory"
"$pg_bindir/initdb" -D "$data_directory" \
    --no-locale --encoding=UTF8 --auth=trust >/dev/null
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

if [[ "$mode" == "contract" ]]; then
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
        IDENTITY_RECEIPT IDENTITY_PROGRAM IDENTITY_INPUT IDENTITY_OUTPUT \
        IDENTITY_ENTITY_0 IDENTITY_ENTITY_1 IDENTITY_ENTITY_2 \
        TRAJECTORY_RECEIPT TRAJECTORY_PROGRAM TRAJECTORY_INPUT TRAJECTORY_OUTPUT \
        TRAJECTORY_CARRIER TRAJECTORY_ENTITY; do
        shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
        psql_arguments+=(-v "$shell_name=$(read_probe_value "$key")")
    done
elif [[ "$mode" != "mutation" ]]; then
    echo "unknown PostgreSQL test mode: $mode" >&2
    exit 64
fi

"$pg_bindir/psql" "${psql_arguments[@]}" -f "$sql_file"
