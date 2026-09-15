#!/usr/bin/env bash
# Serialize explicit chess calibration against other work on this physical host.
set -euo pipefail
repository=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
exec "$repository/tools/host/run-exclusive.sh" python3 "$repository/tools/dependencies/chess_benchmark.py" "$@"
