#!/usr/bin/env bash
# One-time Linux host bootstrap for Laplace-Refactor.
#
# Human/operator boundary:
#   sudo bash scripts/setup-host.sh
#
# This script establishes host prerequisites only. It never builds, packages,
# initializes, migrates, seeds, or activates Laplace. After this succeeds,
# recurring product delivery belongs to CI running as laplace-runner.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY="$(cd -- "$SCRIPT_DIR/.." && pwd)"
RUNNER_USER="laplace-runner"
RUNNER_GROUP="laplace-runner"
RUNNER_HOME="/var/lib/agents/laplace-runner"
RUNNER_SHELL="/usr/sbin/nologin"
SERVICE="laplace-refactor-postgresql.service"
UNIT_SOURCE="$REPOSITORY/packaging/systemd/$SERVICE"
UNIT_TARGET="/etc/systemd/system/$SERVICE"
SUDOERS_TARGET="/etc/sudoers.d/laplace-refactor-postgresql-service"
BOOTSTRAP_RECEIPT="/opt/laplace/receipts/bootstrap/host.json"

if [[ "${EUID}" -ne 0 ]]; then
    if ! command -v sudo >/dev/null 2>&1; then
        echo "setup-host requires root and sudo is unavailable" >&2
        exit 1
    fi
    exec sudo /usr/bin/bash "$0" "$@"
fi

for required in \
    /usr/bin/getent \
    /usr/bin/id \
    /usr/bin/install \
    /usr/bin/python3 \
    /usr/bin/runuser \
    /usr/bin/sha256sum \
    /usr/bin/sudo \
    /usr/bin/systemctl \
    /usr/sbin/groupadd \
    /usr/sbin/useradd \
    /usr/sbin/visudo; do
    if [[ ! -x "$required" ]]; then
        echo "missing host prerequisite: $required" >&2
        exit 1
    fi
done

if [[ ! -f "$UNIT_SOURCE" || -L "$UNIT_SOURCE" ]]; then
    echo "missing tracked static service unit: $UNIT_SOURCE" >&2
    exit 1
fi

# Service identity. Existing identities are verified rather than silently modified.
if ! /usr/bin/getent group "$RUNNER_GROUP" >/dev/null; then
    /usr/sbin/groupadd --system "$RUNNER_GROUP"
fi
if ! /usr/bin/id "$RUNNER_USER" >/dev/null 2>&1; then
    /usr/sbin/useradd \
        --system \
        --gid "$RUNNER_GROUP" \
        --home-dir "$RUNNER_HOME" \
        --shell "$RUNNER_SHELL" \
        --create-home \
        "$RUNNER_USER"
fi

/usr/bin/python3 - "$RUNNER_USER" "$RUNNER_GROUP" "$RUNNER_HOME" "$RUNNER_SHELL" <<'PY'
import grp
import pwd
import sys

user, group, expected_home, expected_shell = sys.argv[1:]
pw = pwd.getpwnam(user)
gr = grp.getgrnam(group)
if pw.pw_gid != gr.gr_gid:
    raise SystemExit("laplace-runner primary group differs")
if pw.pw_dir != expected_home:
    raise SystemExit(f"laplace-runner home differs: {pw.pw_dir}")
if pw.pw_shell != expected_shell:
    raise SystemExit(f"laplace-runner shell differs: {pw.pw_shell}")
PY

# Product/runtime roots. The recurring product owner is laplace-runner. Root owns
# only the static system service file and sudo policy installed below.
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
    /opt/laplace/receipts/postgresql/refactor \
    /opt/laplace/sources \
    /pgtemp \
    /var/lib/pgwal \
    /var/log/laplace \
    /var/log/laplace/postgresql \
    /var/log/laplace/postgresql/refactor; do
    /usr/bin/install -d -o "$RUNNER_USER" -g "$RUNNER_GROUP" -m 0750 "$path"
done

# Keep the product prefix traversable by the service while retaining ownership by
# laplace-runner so /opt/laplace/current can be atomically selected by CI without sudo.
chmod 0755 /opt/laplace
chmod 0755 /opt/laplace/releases

# Runtime database configuration is product state. The parent remains administrator
# owned; only this exact instance directory is delegated to laplace-runner.
/usr/bin/install -d -o root -g root -m 0755 /etc/laplace
/usr/bin/install -d -o root -g root -m 0755 /etc/laplace/instances
/usr/bin/install -d -o "$RUNNER_USER" -g "$RUNNER_GROUP" -m 0750 /etc/laplace/instances/refactor

# Historical bootstrap generations created empty candidate leaves that later fresh
# activation treated as collisions. Remove only exact EMPTY candidate leaves. Never
# remove nonempty PGDATA/WAL/config/log/perfcache state.
/usr/bin/python3 - "$REPOSITORY/contracts/postgresql-cluster.json" <<'PY'
import json
from pathlib import Path
import sys

with Path(sys.argv[1]).open(encoding="utf-8") as stream:
    instance = json.load(stream)["instance"]
for name in ("data_directory", "wal_directory", "temp_directory", "perfcache_directory"):
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

# Static host service envelope. CI selects /opt/laplace/current and writes the product
# configuration; it never rewrites the systemd unit.
/usr/bin/install -o root -g root -m 0644 "$UNIT_SOURCE" "$UNIT_TARGET"
/usr/bin/systemctl daemon-reload
/usr/bin/systemctl enable "$SERVICE" >/dev/null

# Narrow recurring privilege: only service lifecycle. No shell, gateway, package,
# database, Unicode, Highway, receipt, or arbitrary command execution as root.
cat > "$SUDOERS_TARGET" <<EOF
$RUNNER_USER ALL=(root) NOPASSWD: /usr/bin/systemctl start $SERVICE
$RUNNER_USER ALL=(root) NOPASSWD: /usr/bin/systemctl stop $SERVICE
$RUNNER_USER ALL=(root) NOPASSWD: /usr/bin/systemctl restart $SERVICE
EOF
chmod 0440 "$SUDOERS_TARGET"
chown root:root "$SUDOERS_TARGET"
/usr/sbin/visudo -cf "$SUDOERS_TARGET" >/dev/null

for action in start stop restart; do
    /usr/bin/runuser --user "$RUNNER_USER" -- \
        /usr/bin/sudo -n -l /usr/bin/systemctl "$action" "$SERVICE" >/dev/null
done

# Durable bootstrap receipt. It records prerequisites only; product activation state is
# intentionally absent because setup-host is not the product installer/deployer.
TMP_RECEIPT="$(mktemp)"
trap 'rm -f "$TMP_RECEIPT"' EXIT
UNIT_SHA="$(sha256sum "$UNIT_TARGET" | awk '{print $1}')"
SUDOERS_SHA="$(sha256sum "$SUDOERS_TARGET" | awk '{print $1}')"
RUNNER_UID="$(id -u "$RUNNER_USER")"
RUNNER_GID="$(id -g "$RUNNER_USER")"
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
  "static_service": {
    "unit": "$UNIT_TARGET",
    "sha256": "$UNIT_SHA"
  },
  "sudo_policy": {
    "path": "$SUDOERS_TARGET",
    "sha256": "$SUDOERS_SHA",
    "allowed_actions": ["start", "stop", "restart"]
  },
  "product_activated": false,
  "activation_gateway_installed": false
}
EOF
/usr/bin/python3 -m json.tool "$TMP_RECEIPT" >/dev/null
/usr/bin/install -o "$RUNNER_USER" -g "$RUNNER_GROUP" -m 0640 "$TMP_RECEIPT" "$BOOTSTRAP_RECEIPT"

cat <<EOF
Laplace host prerequisites are ready.

Bootstrap stopped here by design. It did not build or activate Laplace and installed
no product activation gateway/key. Recurring work now belongs to CI as laplace-runner.

Bootstrap receipt: $BOOTSTRAP_RECEIPT
Static service:    $UNIT_TARGET
Sudo capability:   systemctl start|stop|restart $SERVICE only
EOF
