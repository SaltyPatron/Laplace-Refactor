#!/usr/bin/env bash
# One-time Linux host bootstrap for Laplace-Refactor.
#
# Human/operator boundary:
#   sudo bash scripts/setup-host.sh
#
# This script does NOT build, package, seed, or activate Laplace. It converges only
# the persistent host prerequisites and the immutable root activation gateway. After
# this succeeds, accepted pushes to main are owned by product-path -> product-activation.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY="$(cd -- "$SCRIPT_DIR/.." && pwd)"
RUNNER_USER="laplace-runner"
GATEWAY="/opt/laplace/deployment/current/bin/laplace-product-activate"
RECEIPT="/opt/laplace/receipts/deployments/product-host-bootstrap.json"

if [[ "${EUID}" -ne 0 ]]; then
    if ! command -v sudo >/dev/null 2>&1; then
        echo "setup-host requires root and sudo is unavailable" >&2
        exit 1
    fi
    exec sudo /usr/bin/bash "$0" "$@"
fi

for required in \
    /usr/bin/python3 \
    /usr/bin/systemctl \
    /usr/bin/runuser \
    /usr/bin/sudo \
    /usr/sbin/groupadd \
    /usr/sbin/useradd \
    /usr/sbin/visudo; do
    if [[ ! -x "$required" ]]; then
        echo "missing host prerequisite: $required" >&2
        exit 1
    fi
done

cd "$REPOSITORY"

# Older host-convergence generations incorrectly created the cluster-owned instance
# leaf directories before cluster activation. Fresh activation intentionally requires
# those leaves to be absent. Remove only exact empty directories named by the current
# cluster contract. Never delete a file, symlink, nonempty directory, PGDATA, WAL,
# receipt, log, or any other product state.
/usr/bin/python3 - "$REPOSITORY/contracts/postgresql-cluster.json" <<'PY'
import json
import os
from pathlib import Path
import sys

contract_path = Path(sys.argv[1])
with contract_path.open(encoding="utf-8") as stream:
    contract = json.load(stream)
instance = contract["instance"]
leaves = [
    Path(instance["data_directory"]),
    Path(instance["wal_directory"]),
    Path(instance["temp_directory"]),
    Path(instance["perfcache_directory"]),
    Path(instance["log_directory"]),
    Path(instance["receipt_directory"]),
]
for path in leaves:
    if path.is_symlink():
        raise SystemExit(f"cluster-owned path is a symlink and bootstrap will not touch it: {path}")
    if not path.exists():
        continue
    if not path.is_dir():
        raise SystemExit(f"cluster-owned path is not a directory and bootstrap will not touch it: {path}")
    try:
        next(path.iterdir())
    except StopIteration:
        path.rmdir()
        print(f"removed empty stale bootstrap residue: {path}")
    else:
        print(f"preserved existing nonempty cluster state: {path}")
PY

# Converge the service identity, persistent parent roots, activation key, immutable
# gateway, scoped sudoers policy, and product service-state systemd units. Cluster
# leaf state (PGDATA/WAL/temp/perfcache/log/instance receipts) remains absent unless
# an already-installed product owns it; cluster activation is its only creator.
/usr/bin/python3 tools/delivery/product_host.py converge \
    --repository "$REPOSITORY" \
    --authorize-system-root \
    --generate-key \
    --output "$RECEIPT"

if [[ ! -x "$GATEWAY" ]]; then
    echo "bootstrap completed without installing the activation gateway: $GATEWAY" >&2
    exit 1
fi

ROOT_PROBE="$(mktemp)"
RUNNER_PROBE="$(mktemp)"
trap 'rm -f "$ROOT_PROBE" "$RUNNER_PROBE"' EXIT

"$GATEWAY" probe > "$ROOT_PROBE"
/usr/bin/runuser --user "$RUNNER_USER" -- \
    /usr/bin/sudo -n "$GATEWAY" probe > "$RUNNER_PROBE"

/usr/bin/python3 - "$RECEIPT" "$ROOT_PROBE" "$RUNNER_PROBE" <<'PY'
import json
import sys

receipt_path, root_probe_path, runner_probe_path = sys.argv[1:]
with open(receipt_path, encoding="utf-8") as stream:
    receipt = json.load(stream)
with open(root_probe_path, encoding="utf-8") as stream:
    root_probe = json.load(stream)
with open(runner_probe_path, encoding="utf-8") as stream:
    runner_probe = json.load(stream)

if receipt.get("schema") != "laplace.product-host-convergence-receipt/v1":
    raise SystemExit("host bootstrap receipt schema differs")
if receipt.get("phase") != "host-ready":
    raise SystemExit("host bootstrap did not reach host-ready")
if receipt.get("product_activated") is not False:
    raise SystemExit("setup-host unexpectedly activated product state")
if root_probe.get("schema") != "laplace.product-activation-gateway-probe/v2":
    raise SystemExit("installed gateway is not the self-upgrading v2 generation")
for field in ("bundle_id", "contract_sha256", "key_fingerprint_sha256"):
    if root_probe.get(field) != runner_probe.get(field):
        raise SystemExit(f"laplace-runner gateway handoff differs for {field}")
PY

cat <<EOF
Laplace host bootstrap complete.

Persistent host authority is installed and laplace-runner can invoke the bounded
activation gateway without a password. This script intentionally did not build or
activate the product. From here, accepted pushes to main are owned by CI/CD:

  product-path -> product-activation -> persistent PostgreSQL/Unicode/Highway readback

Bootstrap receipt: $RECEIPT
Gateway:           $GATEWAY
EOF
