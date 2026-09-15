#!/usr/bin/env bash
# One physical-host exclusion boundary shared by deployment and calibration.
set -euo pipefail
umask 0002
[[ $# -gt 0 ]] || { echo 'usage: run-exclusive.sh COMMAND [ARGUMENT ...]' >&2; exit 64; }
lock=${LAPLACE_HOST_RESOURCE_LOCK:-/build/laplace/work/host-resource.lock}
[[ -d $(dirname -- "$lock") ]] || { echo "Host lock directory is unavailable: $lock" >&2; exit 72; }
[[ ! -L "$lock" ]] || { echo "Host resource lock must not be a symlink: $lock" >&2; exit 65; }
exec 9>>"$lock"
flock --exclusive 9
# Keep the lock in this waiting wrapper, not in long-lived service descendants.
"$@" 9>&-
