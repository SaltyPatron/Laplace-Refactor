#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
python3 "$repo_root/tools/tests/requirements_trace.py" "$repo_root"
python3 "$repo_root/tools/tests/cognition_execution_trace.py" "$repo_root"
python3 "$repo_root/tests/cognition_execution_trace_tests.py"
python3 "$repo_root/tests/selected_source_boundary_tests.py"
python3 "$repo_root/tests/gateway_self_upgrade_tests.py"
python3 "$repo_root/tests/proof_workspace_cleanup_tests.py"
python3 "$repo_root/tests/proof_workspace_foreign_owner_tests.py"
python3 "$repo_root/tests/cluster_candidate_recovery_tests.py"
python3 "$repo_root/tests/product_cluster_upgrade_tests.py"
python3 "$repo_root/tests/postgresql_perfcache_configuration_tests.py"
python3 "$repo_root/tests/product_release_capacity_tests.py"
python3 "$repo_root/tests/postgresql_public_readback_tests.py"
python3 "$repo_root/tests/unicode_retained_admission_tests.py"

python3 "$repo_root/tests/indexed_cognition_upgrade_tests.py"
python3 "$repo_root/tests/highway_product_activation_tests.py"
python3 "$repo_root/tests/highway_retained_context_tests.py"
