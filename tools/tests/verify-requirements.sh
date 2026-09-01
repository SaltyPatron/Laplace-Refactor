#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
python3 "$repo_root/tools/tests/requirements_trace.py" "$repo_root"
python3 "$repo_root/tools/tests/cognition_execution_trace.py" "$repo_root"
python3 "$repo_root/tests/cognition_execution_trace_tests.py"
