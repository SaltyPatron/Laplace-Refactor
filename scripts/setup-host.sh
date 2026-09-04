#!/usr/bin/env bash
# One-time Linux host prerequisite bootstrap for Laplace-Refactor.
#
# Human/operator boundary:
#   sudo bash scripts/setup-host.sh
#
# This script does NOT build, package, install, initialize, migrate, seed, start,
# or activate Laplace/PostgreSQL. It establishes the host prerequisites that CI and
# laplace-runner need, then exits.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY="$(cd -- "$SCRIPT_DIR/.." && pwd)"
RUNNER_USER="laplace-runner"
RUNNER_GROUP="laplace-runner"
RUNNER_HOME="/var/lib/agents/laplace-runner"
RUNNER_SHELL="/usr/sbin/nologin"
SERVICE="laplace-refactor-postgresql.service"
SUDOERS_TARGET="/etc/sudoers.d/laplace-refactor-postgresql-service"
BOOTSTRAP_RECEIPT="/opt/laplace/receipts/bootstrap/host.json"

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
VISUDO_BIN="$(resolve_command visudo)"
CHMOD_BIN="$(resolve_command chmod)"
CHOWN_BIN="$(resolve_command chown)"
MKTEMP_BIN="$(resolve_command mktemp)"
AWK_BIN="$(resolve_command awk)"
RM_BIN="$(resolve_command rm)"

cd "$REPOSITORY"

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

# Persistent service-owned PARENT roots. Instance leaves such as PGDATA, WAL,
# perfcache, instance config/log/receipt directories are deliberately not created;
# recurring CI/product lifecycle owns those exact leaves.
for path in \
    /build/laplace \
    /build/laplace/runner \
    /opt/laplace \
    /opt/laplace/releases \
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
    "$INSTALL_BIN" -d -o "$RUNNER_USER" -g "$RUNNER_GROUP" -m 0750 "$path"
done

# Product prefix is service-owned but traversable. CI can atomically manage
# /opt/laplace/current and content-addressed releases without recurring sudo.
"$CHMOD_BIN" 0755 /opt/laplace
"$CHMOD_BIN" 0755 /opt/laplace/releases

# Host-owned parent only. CI/product lifecycle owns the refactor instance config leaf.
"$INSTALL_BIN" -d -o root -g root -m 0755 /etc/laplace
"$INSTALL_BIN" -d -o root -g root -m 0755 /etc/laplace/instances

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

# Narrow recurring privilege only. The unit itself and product lifecycle are not
# installed or executed here. This policy cannot invoke a shell, package installer,
# database controller, Unicode/Highway operation, or whole-product gateway as root.
cat > "$SUDOERS_TARGET" <<EOF
$RUNNER_USER ALL=(root) NOPASSWD: $SYSTEMCTL_BIN start $SERVICE
$RUNNER_USER ALL=(root) NOPASSWD: $SYSTEMCTL_BIN stop $SERVICE
$RUNNER_USER ALL=(root) NOPASSWD: $SYSTEMCTL_BIN restart $SERVICE
EOF
"$CHMOD_BIN" 0440 "$SUDOERS_TARGET"
"$CHOWN_BIN" root:root "$SUDOERS_TARGET"
"$VISUDO_BIN" -cf "$SUDOERS_TARGET" >/dev/null

# Validate the exact recurring privilege as laplace-runner without depending on
# util-linux runuser or any hard-coded runuser path. The outer sudo only switches
# identity because setup-host is already root; the inner sudo proves the NOPASSWD rule.
for action in start stop restart; do
    "$SUDO_BIN" -u "$RUNNER_USER" -- \
        "$SUDO_BIN" -n -l "$SYSTEMCTL_BIN" "$action" "$SERVICE" >/dev/null
done

# Durable bootstrap receipt: prerequisites only, explicitly not product state.
TMP_RECEIPT="$("$MKTEMP_BIN")"
trap '"$RM_BIN" -f "$TMP_RECEIPT"' EXIT
SUDOERS_SHA="$("$SHA256SUM_BIN" "$SUDOERS_TARGET" | "$AWK_BIN" '{print $1}')"
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

cat <<EOF
Laplace host prerequisites are ready.

setup-host stopped here by design. It did not build, install, initialize, start,
seed, or activate the product. Recurring product delivery now belongs to CI as
$RUNNER_USER.

Bootstrap receipt: $BOOTSTRAP_RECEIPT
Sudo capability:   $SYSTEMCTL_BIN start|stop|restart $SERVICE only
EOF
