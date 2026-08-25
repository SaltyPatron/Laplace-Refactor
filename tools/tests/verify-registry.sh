#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 CTEST-BUILD-DIRECTORY" >&2
    exit 64
fi

build_directory=$1
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
registry="$repo_root/tests/registry.json"
requirements="$repo_root/requirements/product.yaml"

if [[ ! -d "$build_directory" ]]; then
    echo "CTest build directory does not exist: $build_directory" >&2
    exit 66
fi

duplicate_count=$(jq '[.tests | group_by(.ctest_name)[] | select(length != 1)] | length' "$registry")
if [[ "$duplicate_count" != "0" ]]; then
    echo "test registry contains duplicate names" >&2
    exit 65
fi

unmapped_count=$(jq '[.tests[] | select((.evidence_targets | length) == 0)] | length' "$registry")
if [[ "$unmapped_count" != "0" ]]; then
    echo "test registry contains entries without evidence targets" >&2
    exit 65
fi

while IFS= read -r evidence_id; do
    if ! rg -q "^[[:space:]]+- ${evidence_id}$" "$requirements"; then
        echo "test registry references unknown evidence target: $evidence_id" >&2
        exit 65
    fi
done < <(jq -r '[.tests[].evidence_targets[]] | unique[]' "$registry")

differences=$(comm -3 \
    <(ctest --test-dir "$build_directory" --show-only=json-v1 | jq -r '.tests[].name' | sort) \
    <(jq -r '.tests[].ctest_name' "$registry" | sort))
if [[ -n "$differences" ]]; then
    echo "CTest and registry names differ:" >&2
    echo "$differences" >&2
    exit 65
fi

printf 'verified %s registered tests against %s\n' \
    "$(jq '.tests | length' "$registry")" "$build_directory"
