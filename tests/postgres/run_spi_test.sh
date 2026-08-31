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
temporary_parent=${RUNNER_TEMP:-${TMPDIR:-/tmp}}
test_root=$(mktemp -d "$temporary_parent/laplace-postgres-test.XXXXXX")
data_directory="$test_root/data"
socket_directory=$(mktemp -d /tmp/lp-pg.XXXXXX)
server_log="$test_root/postgres.log"
port=55432
server_started=0
perfcache_root="$test_root/perfcache-root"
mkdir -p -- "$perfcache_root"
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
postgres_options="-k $socket_directory -p $port -c listen_addresses= -c max_prepared_transactions=4 -c laplace.perfcache_root=$perfcache_root -c extension_control_path=$control_root -c dynamic_library_path=$module_directory:$postgres_library_directory:$engine_directory"
if [[ "$mode" != "composition-measurement" ]]; then
    postgres_options="-F $postgres_options"
else
    postgres_options="$postgres_options -c shared_buffers=1GB -c max_wal_size=8GB -c checkpoint_timeout=30min -c track_io_timing=on -c track_wal_io_timing=on"
fi
if [[ "$mode" == "unicode-root" || "$mode" == "unicode-access-mutation" ||
      "$mode" == "source-admission" || "$mode" == "iso-639-admission" ||
      "$mode" == "cili-admission" ]]; then
    postgres_options="$postgres_options -c shared_buffers=512MB -c max_wal_size=8GB -c checkpoint_timeout=30min"
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
        WORLD_PROFILE_ID WORLD_OCCURRENCE_ID WORLD_EVIDENCE_NODE \
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
elif [[ "$mode" == "standing" || "$mode" == "standing-mutation" ]]; then
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
    if [[ "$mode" == "standing-mutation" ]]; then
        if [[ -z "${LAPLACE_MUTANT_MODULE:-}" || ! -f "$LAPLACE_MUTANT_MODULE" ]]; then
            echo "standing replay mutant module is missing" >&2
            exit 66
        fi
        psql_arguments+=(-v "standing_mutant_module=$LAPLACE_MUTANT_MODULE")
    fi
elif [[ "$mode" == "perfcache-mutation" ]]; then
    if [[ -z "${LAPLACE_MUTANT_MODULE:-}" || ! -f "$LAPLACE_MUTANT_MODULE" ]]; then
        echo "perfcache mutant module is missing" >&2
        exit 66
    fi
    psql_arguments+=(-v "perfcache_mutant_module=$LAPLACE_MUTANT_MODULE")
elif [[ "$mode" == "unicode-root" || "$mode" == "unicode-access-mutation" ||
      "$mode" == "source-admission" || "$mode" == "iso-639-admission" ||
      "$mode" == "cili-admission" ]]; then
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
    if [[ "$mode" == "iso-639-admission" ]]; then
        iso_source_root=${LAPLACE_ISO_639_SOURCE_ROOT:-/vault/Data/ISO639}
        if [[ ! -d "$iso_source_root" ]]; then
            echo "verified ISO 639 source root is unavailable: $iso_source_root" >&2
            exit 77
        fi
        probe_output=$(LD_LIBRARY_PATH="$engine_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
            "$native_probe" "$iso_source_root")
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
            "$native_probe" "$cili_source_root")
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
    if [[ "$mode" == "source-admission" ]]; then
        probe_output=$("$native_probe")
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

if [[ -n "${variable_file:-}" ]]; then
    "$pg_bindir/psql" "${psql_arguments[@]}" -f "$variable_file" -f "$sql_file"
else
    "$pg_bindir/psql" "${psql_arguments[@]}" -f "$sql_file"
fi

if [[ "$mode" == "unicode-root" || "$mode" == "unicode-access-mutation" ||
      "$mode" == "source-admission" || "$mode" == "iso-639-admission" ||
      "$mode" == "cili-admission" ]]; then
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
