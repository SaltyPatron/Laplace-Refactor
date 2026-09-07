#!/usr/bin/env bash
# Release unallocated huge pages from Refactor so the existing 5432 cluster can start.
set -euo pipefail

mode=${1:---inspect}
case "$mode" in --inspect|--apply) ;; *) exit 2 ;; esac
[[ $(id -un) == laplace-runner ]] || { echo 'Run through the PostgreSQL service owner.' >&2; exit 1; }

primary=/opt/laplace/pgsql-18
primary_data=/opt/laplace/pgdata/data
selected=/opt/laplace/runtime/refactor/pgsql-18
config=/etc/laplace/instances/refactor/postgresql.conf
socket=/opt/laplace/runtime/postgresql/refactor
service=laplace-refactor-postgresql.service
psql=("$selected/bin/psql" -X -w -A -t -v ON_ERROR_STOP=1 -h "$socket" -p 55433 -U laplace_admin -d laplace_refactor)
export PGCONNECT_TIMEOUT=5

primary_huge_pages=$("$primary/bin/postgres" -D "$primary_data" -C huge_pages)
[[ $primary_huge_pages == on ]] || { echo "5432 huge_pages=$primary_huge_pages; this repair does not apply." >&2; exit 1; }
[[ $("${psql[@]}" -c 'SHOW config_file') == "$config" ]] || { echo 'Unexpected Refactor configuration owner.' >&2; exit 1; }
before_id=$("${psql[@]}" -c 'SELECT system_identifier FROM pg_control_system()')
before_start=$("${psql[@]}" -c 'SELECT pg_postmaster_start_time()')
printf 'primary_huge_pages=%s\nrefactor_system_identifier=%s\n' "$primary_huge_pages" "$before_id"
"${psql[@]}" -c "SELECT name, setting FROM pg_settings WHERE name IN ('huge_pages','huge_pages_status','shared_memory_size_in_huge_pages') ORDER BY name"
awk '/^(HugePages_Total|HugePages_Free|HugePages_Rsvd|Hugepagesize):/ {print}' /proc/meminfo
[[ $mode == --apply ]] || exit 0

# Establish the existing narrowly scoped restart permission before changing a file.
sudo -n -l /usr/bin/systemctl restart "$service" >/dev/null
[[ -w $config && ! -L $config ]] || { echo 'Refactor configuration is not an owned regular file.' >&2; exit 1; }
receipt=/opt/laplace/receipts/postgresql/refactor/huge-page-recovery/$(date -u +%Y%m%dT%H%M%S.%NZ)
mkdir -p -m 0700 "$receipt"
cp -p "$config" "$receipt/postgresql.conf.before"
sha256sum "$config" > "$receipt/config-before.sha256"
printf '%s\n' "$before_id" > "$receipt/system-identifier.before"
printf '%s\n' "$before_start" > "$receipt/postmaster-started.before"
awk '/^(HugePages_Total|HugePages_Free|HugePages_Rsvd|Hugepagesize):/ {print}' /proc/meminfo > "$receipt/huge-pages.before"

stage=$(mktemp "$config.XXXXXX")
changed=0
confirmed=0
cleanup() {
    status=$?
    trap - EXIT
    if (( status != 0 && changed == 1 && confirmed == 0 )); then
        # Restore the previous configuration if Refactor cannot restart and
        # prove the same cluster. Never replace or initialize either data tree.
        cp -p "$receipt/postgresql.conf.before" "$config"
        sync -f "$config"
        sudo -n /usr/bin/systemctl restart "$service" || true
    fi
    [[ ! -f $stage ]] || unlink "$stage"
    exit "$status"
}
trap cleanup EXIT
awk '
    $1 == "huge_pages" && $2 == "=" {
        seen++
        if (seen != 1 || ($3 != "try" && $3 != "on" && $3 != "off")) exit 2
        print "huge_pages = off"
        next
    }
    { print }
    END { if (seen != 1) exit 2 }
' "$config" > "$stage"
chmod --reference="$config" "$stage"
sync -f "$stage"
changed=1
mv -f "$stage" "$config"
sync -f "${config%/*}"
[[ $("$selected/bin/postgres" -D /opt/laplace/pgdata/refactor/data -c "config_file=$config" -C huge_pages) == off ]] || {
    echo 'Another configuration source overrides huge_pages; restoring the original.' >&2
    exit 1
}
sudo -n /usr/bin/systemctl restart "$service"

wait_ready() {
    local binary=$1 host=$2 port=$3 attempts=$4
    for ((i=0; i<attempts; ++i)); do
        if "$binary" -q -t 1 -h "$host" -p "$port"; then return 0; fi
        sleep 1
    done
    return 1
}
wait_ready "$selected/bin/pg_isready" "$socket" 55433 60
[[ $("${psql[@]}" -c 'SELECT system_identifier FROM pg_control_system()') == "$before_id" ]]
[[ $("${psql[@]}" -c 'SELECT pg_postmaster_start_time()') != "$before_start" ]]
[[ $("${psql[@]}" -c 'SHOW huge_pages') == off ]]
confirmed=1
sha256sum "$config" > "$receipt/config-after.sha256"
"${psql[@]}" -c "SELECT current_database(), current_user, system_identifier, pg_postmaster_start_time(), current_setting('huge_pages'), current_setting('huge_pages_status') FROM pg_control_system()" > "$receipt/refactor-after.txt"
awk '/^(HugePages_Total|HugePages_Free|HugePages_Rsvd|Hugepagesize):/ {print}' /proc/meminfo > "$receipt/huge-pages.after"

# The primary service already retries startup. Use its installed restart grant
# when available to avoid waiting for the failed readiness loop to finish.
if sudo -n -l /usr/bin/systemctl restart laplace-postgresql.service >/dev/null 2>&1; then
    sudo -n /usr/bin/systemctl restart laplace-postgresql.service
fi
wait_ready "$primary/bin/pg_isready" /var/run/postgresql 5432 180
"$primary/bin/psql" -X -w -A -t -v ON_ERROR_STOP=1 -h /var/run/postgresql -p 5432 -U laplace_admin -d postgres \
    -c "SELECT current_database(), current_user, system_identifier, pg_postmaster_start_time(), current_setting('port'), current_setting('listen_addresses'), current_setting('huge_pages_status') FROM pg_control_system()" > "$receipt/primary-after.txt"
"$primary/bin/pg_isready" -h 127.0.0.1 -p 5432 > "$receipt/tcp-readiness.txt"
cat "$receipt/refactor-after.txt" "$receipt/primary-after.txt" "$receipt/tcp-readiness.txt"
printf 'recovery_receipt=%s\n' "$receipt"
