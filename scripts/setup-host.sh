#!/usr/bin/env bash
# Default: prepare the host, install/activate the configured product, and verify it.
# sudo bash scripts/setup-host.sh
# Explicit limited modes: prerequisites, storage.

set -euo pipefail
umask 0002

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY="$(cd -- "$SCRIPT_DIR/.." && pwd)"
RUNNER_USER="laplace-runner"
RUNNER_GROUP="laplace-runner"
RUNNER_HOME="/var/lib/agents/laplace-runner"
RUNNER_SHELL="/usr/sbin/nologin"
SERVICE="laplace-refactor-postgresql.service"
SERVICE_SOURCE="$REPOSITORY/packaging/systemd/$SERVICE"
SERVICE_TARGET="/etc/systemd/system/$SERVICE"
SUDOERS_TARGET="/etc/sudoers.d/laplace-refactor-postgresql-service"
BOOTSTRAP_RECEIPT="/opt/laplace/receipts/bootstrap/host.json"
MODE="${1:-setup}"
[[ "$MODE" == setup || "$MODE" == prerequisites || "$MODE" == storage ]] || { echo "usage: $0 [setup|prerequisites|storage]" >&2; exit 2; }

resolve_command() {
    local name="$1"
    local resolved
    resolved="$(command -v "$name" 2>/dev/null || true)"
    if [[ -z "$resolved" || ! -x "$resolved" ]]; then
        echo "missing host prerequisite command: $name" >&2
        exit 1
    fi
    printf '%s\n' "$resolved"
}

if [[ "${EUID}" -ne 0 ]]; then
    SUDO_BIN="$(resolve_command sudo)"
    BASH_BIN="$(resolve_command bash)"
    exec "$SUDO_BIN" "$BASH_BIN" "$0" "$@"
fi

GETENT_BIN="$(resolve_command getent)"
ID_BIN="$(resolve_command id)"
INSTALL_BIN="$(resolve_command install)"
PYTHON_BIN="$(resolve_command python3)"
SHA256SUM_BIN="$(resolve_command sha256sum)"
SUDO_BIN="$(resolve_command sudo)"
SYSTEMCTL_BIN="$(resolve_command systemctl)"
GROUPADD_BIN="$(resolve_command groupadd)"
USERADD_BIN="$(resolve_command useradd)"
USERMOD_BIN="$(resolve_command usermod)"
VISUDO_BIN="$(resolve_command visudo)"
CHMOD_BIN="$(resolve_command chmod)"
CHOWN_BIN="$(resolve_command chown)"
MKTEMP_BIN="$(resolve_command mktemp)"
AWK_BIN="$(resolve_command awk)"
RM_BIN="$(resolve_command rm)"

cd "$REPOSITORY"

if [[ ! -f "$SERVICE_SOURCE" || -L "$SERVICE_SOURCE" ]]; then
    echo "static PostgreSQL service definition is absent or unsafe: $SERVICE_SOURCE" >&2
    exit 1
fi

# Establish the service/runner identity only when absent. An already-running GitHub
# runner account is not rewritten merely because its login metadata differs from a
# fresh-install preference.
if ! "$GETENT_BIN" group "$RUNNER_GROUP" >/dev/null; then
    "$GROUPADD_BIN" --system "$RUNNER_GROUP"
fi
if ! "$ID_BIN" "$RUNNER_USER" >/dev/null 2>&1; then
    "$USERADD_BIN" \
        --system \
        --gid "$RUNNER_GROUP" \
        --home-dir "$RUNNER_HOME" \
        --shell "$RUNNER_SHELL" \
        --create-home \
        "$RUNNER_USER"
fi

"$PYTHON_BIN" - "$RUNNER_USER" "$RUNNER_GROUP" <<'PY'
import grp
import pwd
import sys

user, group = sys.argv[1:]
pw = pwd.getpwnam(user)
gr = grp.getgrnam(group)
if pw.pw_gid != gr.gr_gid:
    raise SystemExit(
        f"{user} primary group differs: gid={pw.pw_gid}, expected {group} gid={gr.gr_gid}"
    )
PY

# The invoking operator and CI use the same group. Existing user ownership is
# preserved; setgid and group write are the shared storage boundary.
operator="${LAPLACE_OPERATOR:-${SUDO_USER:-}}"
if [[ -n "$operator" && "$operator" != root ]]; then
    "$ID_BIN" "$operator" >/dev/null
    "$USERMOD_BIN" -aG "$RUNNER_GROUP" "$operator"
fi
for volume in /build /opt/laplace/pgdata /var/lib/pgwal /pgtemp; do
    mountpoint -q "$volume" || { echo "missing storage mount: $volume" >&2; exit 1; }
done

# Persistent group-shared PARENT roots. Instance leaves such as PGDATA, WAL,
# perfcache, instance config/log/receipt directories are deliberately not created;
# recurring CI/product lifecycle owns those exact leaves.
for path in \
    /build/laplace \
    /build/laplace/runner \
    /build/laplace/build \
    /build/laplace/work \
    /build/laplace/work/refactor-scratch \
    /build/laplace/worktrees \
    /build/laplace/recovery \
    /opt/laplace \
    /opt/laplace/releases \
    /opt/laplace/runtime \
    /opt/laplace/runtime/postgresql \
    /opt/laplace/pgdata \
    /opt/laplace/pgdata/refactor \
    /opt/laplace/receipts \
    /opt/laplace/receipts/bootstrap \
    /opt/laplace/receipts/postgresql \
    /opt/laplace/sources \
    /pgtemp \
    /var/lib/pgwal \
    /var/log/laplace \
    /var/log/laplace/postgresql; do
    if [[ -L "$path" || -e "$path/PG_VERSION" ]]; then
        echo "expected a physical shared parent: $path" >&2
        exit 1
    fi
    "$INSTALL_BIN" -d -g "$RUNNER_GROUP" -m 2770 "$path"
done

# An existing socket leaf needs operator group traversal; fresh activation owns creation.
if [[ -d /opt/laplace/runtime/postgresql/refactor && ! -L /opt/laplace/runtime/postgresql/refactor ]]; then
    chgrp "$RUNNER_GROUP" /opt/laplace/runtime/postgresql/refactor
    chmod 2770 /opt/laplace/runtime/postgresql/refactor
fi

# Product prefix is service-owned but traversable. CI can atomically manage
# /opt/laplace/current, /opt/laplace/runtime/refactor and content-addressed releases
# without recurring sudo.
"$CHMOD_BIN" 2775 /opt/laplace
"$CHMOD_BIN" 2775 /opt/laplace/releases

# Published trees and recovery evidence retain their exact recorded metadata.
PUBLISHED_INPUTS=$(readlink -m /opt/laplace/package-inputs/postgresql)
PUBLISHED_RELEASES=$(readlink -m /opt/laplace/releases)
for workspace in /build/laplace/build /build/laplace/work /build/laplace/worktrees \
    /build/laplace/runner/product/build /build/laplace/runner/product/locks; do
    [[ -d "$workspace" && ! -L "$workspace" ]] || continue
    find "$workspace" -xdev \( -path "$PUBLISHED_INPUTS" -o -path "$PUBLISHED_RELEASES" \) -prune -o ! -type l -exec chgrp "$RUNNER_GROUP" {} +
    find "$workspace" -xdev \( -path "$PUBLISHED_INPUTS" -o -path "$PUBLISHED_RELEASES" \) -prune -o -type d -exec chmod g+rws {} +
    find "$workspace" -xdev \( -path "$PUBLISHED_INPUTS" -o -path "$PUBLISHED_RELEASES" \) -prune -o -type f -exec chmod g+rwX {} +
done

export TMPDIR=/build/laplace/work/refactor-scratch TMP=/build/laplace/work/refactor-scratch TEMP=/build/laplace/work/refactor-scratch

# The Refactor runner must create group-writable artifacts on every job, even
# when an individual build tool does not set its own process umask.
RUNNER_UNIT=actions.runner.SaltyPatron-Laplace-Refactor.hart-server-refactor.service
if "$SYSTEMCTL_BIN" cat "$RUNNER_UNIT" >/dev/null 2>&1; then
    [[ $("$SYSTEMCTL_BIN" show "$RUNNER_UNIT" -p User --value) == "$RUNNER_USER" ]] || {
        echo "runner service identity differs: $RUNNER_UNIT" >&2; exit 1;
    }
    "$INSTALL_BIN" -d -m 0755 "/etc/systemd/system/$RUNNER_UNIT.d"
    cat > "/etc/systemd/system/$RUNNER_UNIT.d/50-laplace-storage.conf" <<EOF
[Unit]
RequiresMountsFor=/build /var/lib/agents

[Service]
Group=$RUNNER_GROUP
UMask=0002
Environment=TMPDIR=$TMPDIR
Environment=TMP=$TMP
Environment=TEMP=$TEMP
EOF
    runner_directory="$RUNNER_HOME/actions-runner-refactor"
    if [[ -d "$runner_directory" ]]; then
        path_seed=$(mktemp "$TMPDIR/runner-path.XXXXXXXX")
        printf '%s\n' '/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin' > "$path_seed"
        "$INSTALL_BIN" -o "$RUNNER_USER" -g "$RUNNER_GROUP" -m 0664 "$path_seed" "$runner_directory/.path"
        rm -- "$path_seed"
    fi
    "$SYSTEMCTL_BIN" daemon-reload
    "$SYSTEMCTL_BIN" try-restart "$RUNNER_UNIT"
fi
if [[ "$MODE" == storage ]]; then
    echo "Shared parent permissions and Refactor runner storage environment repaired."
    exit 0
fi

# The host owns the /etc namespace; laplace-runner owns the product's instance
# configuration content through this bounded group-writable parent.
"$INSTALL_BIN" -d -o root -g root -m 0755 /etc/laplace
"$INSTALL_BIN" -d -o root -g "$RUNNER_GROUP" -m 2770 /etc/laplace/instances

# Older bootstrap generations created candidate leaves before activation. Remove only
# exact EMPTY leaves. Never remove a symlink, file, nonempty PGDATA/WAL/config/log/
# perfcache/receipt directory, or any other product state.
"$PYTHON_BIN" - "$REPOSITORY/contracts/postgresql-cluster.json" <<'PY'
import json
from pathlib import Path
import sys

with Path(sys.argv[1]).open(encoding="utf-8") as stream:
    instance = json.load(stream)["instance"]

for name in (
    "data_directory",
    "wal_directory",
    "temp_directory",
    "perfcache_directory",
    "config_directory",
    "log_directory",
    "receipt_directory",
):
    path = Path(instance[name])
    if path.is_symlink():
        raise SystemExit(f"candidate path is a symlink and bootstrap will not touch it: {path}")
    if not path.exists():
        continue
    if not path.is_dir():
        raise SystemExit(f"candidate path is not a directory and bootstrap will not touch it: {path}")
    try:
        next(path.iterdir())
    except StopIteration:
        path.rmdir()
        print(f"removed empty stale candidate leaf: {path}")
    else:
        print(f"preserved existing nonempty candidate state: {path}")
PY

# Historical delivery generations split one package's evidence across root-level
# packages/plans/deployments and several instance-local namespaces. Converge only
# mechanically identified bytes into the current package-addressed cluster-activation
# tree. Unknown or conflicting evidence remains in place and is reported; the empty
# obsolete root-owned deployments directory is removed only here under root authority.
RECEIPT_CONVERGENCE="$(
    "$PYTHON_BIN" tools/delivery/receipt_estate.py \
        --root / \
        --cluster-contract contracts/postgresql-cluster.json \
        --service-user "$RUNNER_USER" \
        --service-group "$RUNNER_GROUP" \
        --authorize-system-root
)"
printf '%s\n' "$RECEIPT_CONVERGENCE" | "$PYTHON_BIN" -c '
import json, sys
value=json.load(sys.stdin)
assert value["schema"] == "laplace.receipt-estate-convergence/v1"
assert isinstance(value["migration_count"], int)
assert isinstance(value["preserved_unknown_or_conflicting"], list)
print(
    "receipt estate converged: "
    f"migrated={value['"'"'migration_count'"'"']} "
    f"preserved={len(value['"'"'preserved_unknown_or_conflicting'"'"'])}"
)
'

# Install one static OS service envelope. It runs as laplace-runner and points only at
# the runner-owned /opt/laplace/runtime/refactor link. No package generation, database
# operation, or semantic action is encoded in this bootstrap step.
"$INSTALL_BIN" -o root -g root -m 0644 "$SERVICE_SOURCE" "$SERVICE_TARGET"
"$SYSTEMCTL_BIN" daemon-reload
"$SYSTEMCTL_BIN" enable "$SERVICE" >/dev/null
"$SYSTEMCTL_BIN" is-enabled --quiet "$SERVICE"

# Narrow recurring privilege only. This policy cannot invoke a shell, package
# installer, database controller, Unicode/Highway operation, or whole-product gateway
# as root.
cat > "$SUDOERS_TARGET" <<EOF
$RUNNER_USER ALL=(root) NOPASSWD: $SYSTEMCTL_BIN start $SERVICE
$RUNNER_USER ALL=(root) NOPASSWD: $SYSTEMCTL_BIN stop $SERVICE
$RUNNER_USER ALL=(root) NOPASSWD: $SYSTEMCTL_BIN restart $SERVICE
$RUNNER_USER ALL=(root) NOPASSWD: $SYSTEMCTL_BIN restart laplace-refactor-cognition.service
EOF
"$CHMOD_BIN" 0440 "$SUDOERS_TARGET"
"$CHOWN_BIN" root:root "$SUDOERS_TARGET"
"$VISUDO_BIN" -cf "$SUDOERS_TARGET" >/dev/null

for action in start stop restart; do
    "$SUDO_BIN" -u "$RUNNER_USER" -- \
        "$SUDO_BIN" -n -l "$SYSTEMCTL_BIN" "$action" "$SERVICE" >/dev/null
done

# Durable bootstrap receipt: host envelope only, explicitly not product state.
TMP_RECEIPT="$("$MKTEMP_BIN")"
trap '"$RM_BIN" -f "$TMP_RECEIPT"' EXIT
SUDOERS_SHA="$("$SHA256SUM_BIN" "$SUDOERS_TARGET" | "$AWK_BIN" '{print $1}')"
SERVICE_SHA="$("$SHA256SUM_BIN" "$SERVICE_TARGET" | "$AWK_BIN" '{print $1}')"
RUNNER_UID="$("$ID_BIN" -u "$RUNNER_USER")"
RUNNER_GID="$("$ID_BIN" -g "$RUNNER_USER")"
cat > "$TMP_RECEIPT" <<EOF
{
  "schema": "laplace.host-bootstrap/v1",
  "phase": "host-prerequisites-ready",
  "service_identity": {
    "user": "$RUNNER_USER",
    "group": "$RUNNER_GROUP",
    "uid": $RUNNER_UID,
    "gid": $RUNNER_GID
  },
  "service_envelope": {
    "path": "$SERVICE_TARGET",
    "sha256": "$SERVICE_SHA",
    "enabled": true,
    "started_by_bootstrap": false
  },
  "sudo_policy": {
    "path": "$SUDOERS_TARGET",
    "sha256": "$SUDOERS_SHA",
    "service": "$SERVICE",
    "systemctl": "$SYSTEMCTL_BIN",
    "allowed_actions": ["start", "stop", "restart"]
  },
  "product_activated": false,
  "postgresql_initialized": false,
  "activation_gateway_installed": false
}
EOF
"$PYTHON_BIN" -m json.tool "$TMP_RECEIPT" >/dev/null
"$INSTALL_BIN" -o "$RUNNER_USER" -g "$RUNNER_GROUP" -m 0640 \
    "$TMP_RECEIPT" "$BOOTSTRAP_RECEIPT"

if [[ "$MODE" == prerequisites ]]; then
    echo "Host prerequisites completed. Receipt: $BOOTSTRAP_RECEIPT"
    exit 0
fi

"$SUDO_BIN" -u "$RUNNER_USER" -H -- bash "$SCRIPT_DIR/setup-product.sh"
COGNITION_SERVICE=laplace-refactor-cognition.service
"$INSTALL_BIN" -o root -g root -m 0644 "$REPOSITORY/packaging/systemd/$COGNITION_SERVICE" "/etc/systemd/system/$COGNITION_SERVICE"
"$SYSTEMCTL_BIN" daemon-reload
"$SYSTEMCTL_BIN" enable "$COGNITION_SERVICE"
"$SYSTEMCTL_BIN" restart "$COGNITION_SERVICE"
"$SYSTEMCTL_BIN" is-active --quiet "$COGNITION_SERVICE"
for attempt in {1..30}; do
    if "$SUDO_BIN" -u "$RUNNER_USER" -H -- /opt/laplace/runtime/refactor/bin/laplace-cognition --relations constituent AA > "$TMPDIR/setup-cognition-readback" 2>/dev/null; then
        [[ $(cat "$TMPDIR/setup-cognition-readback") == A ]] && break
    fi
    [[ "$attempt" != 30 ]] || { echo 'Installed cognition readback failed' >&2; exit 1; }
    sleep 1
done
echo "Laplace setup completed: PostgreSQL, Unicode, Highway and the cognition service are running and verified."
