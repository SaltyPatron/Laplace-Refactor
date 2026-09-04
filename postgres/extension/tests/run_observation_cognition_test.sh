#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 7 ]]; then
    echo "usage: $0 PG-BINDIR CONTROL-ROOT MODULE-DIRECTORY ENGINE-DIRECTORY NATIVE-PROBE SQL-FILE SANITIZER-PRELOAD" >&2
    exit 64
fi

pg_bindir=$1
postgres_library_directory=$("$pg_bindir/pg_config" --pkglibdir)
control_root=$2
module_directory=$3
engine_directory=$4
native_probe=$5
sql_file=$6
sanitizer_preload=$7
temporary_parent=${RUNNER_TEMP:-${TMPDIR:-/tmp}}
test_root=$(mktemp -d "$temporary_parent/laplace-postgres-observation-cognition.XXXXXX")
data_directory="$test_root/data"
socket_directory=$(mktemp -d /tmp/lp-oc-pg.XXXXXX)
server_log="$test_root/postgres.log"
port=${LAPLACE_POSTGRES_OBSERVATION_TEST_PORT:-55442}
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
    if [[ "$socket_directory" == /tmp/lp-oc-pg.* ]]; then
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

if [[ ! -x "$native_probe" ]]; then
    echo "observation cognition native oracle is unavailable" >&2
    exit 65
fi
probe_output=$(
    LD_LIBRARY_PATH="$engine_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$native_probe"
)

psql_arguments=(
    -X
    -h "$socket_directory"
    -p "$port"
    -d postgres
    -v ON_ERROR_STOP=1
)
while IFS='=' read -r key value; do
    [[ -n "$key" && -n "$value" ]] || continue
    if [[ ! "$key" =~ ^[A-Z0-9_]+$ || ! "$value" =~ ^[0-9a-f]+$ ]]; then
        echo "invalid observation cognition oracle output: $key=$value" >&2
        exit 66
    fi
    shell_name=$(tr '[:upper:]' '[:lower:]' <<<"$key")
    psql_arguments+=(-v "$shell_name=$value")
done <<<"$probe_output"

LD_PRELOAD="${sanitizer_preload}${sanitizer_preload:+${LD_PRELOAD:+:}}${LD_PRELOAD:-}" \
"$pg_bindir/psql" "${psql_arguments[@]}" -f "$sql_file"
