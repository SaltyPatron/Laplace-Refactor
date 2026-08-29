#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "usage: $0 CTEST-BUILD-DIRECTORY [PROFILE]" >&2
    exit 64
fi

build_directory=$1
profile=${2:-core}
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
registry="$repo_root/tests/registry.json"
requirements="$repo_root/requirements/product.yaml"

if [[ ! -d "$build_directory" ]]; then
    echo "CTest build directory does not exist: $build_directory" >&2
    exit 66
fi

registry_inputs=("$registry")
shopt -s nullglob
for fragment in "$repo_root"/tests/registry.d/*.json; do
    registry_inputs+=("$fragment")
done
shopt -u nullglob
merged_registry=$(mktemp)
trap 'rm -f "$merged_registry"' EXIT
jq -s '{schema: .[0].schema, tests: ([.[].tests[]])}' \
    "${registry_inputs[@]}" > "$merged_registry"

duplicate_count=$(jq '[.tests | group_by(.ctest_name)[] | select(length != 1)] | length' "$merged_registry")
if [[ "$duplicate_count" != "0" ]]; then
    echo "test registry contains duplicate names" >&2
    exit 65
fi

unmapped_count=$(jq '[.tests[] | select((.evidence_targets | length) == 0)] | length' "$merged_registry")
if [[ "$unmapped_count" != "0" ]]; then
    echo "test registry contains entries without evidence targets" >&2
    exit 65
fi

while IFS= read -r evidence_id; do
    if ! grep -Fqx "      - ${evidence_id}" "$requirements"; then
        echo "test registry references unknown evidence target: $evidence_id" >&2
        exit 65
    fi
done < <(jq -r '[.tests[].evidence_targets[]] | unique[]' "$merged_registry")

differences=$(comm -3 \
    <(ctest --test-dir "$build_directory" --show-only=json-v1 | jq -r '.tests[].name' | sort) \
    <(jq -r --arg profile "$profile" \
        '.tests[] | select((.profiles == null) or ((.profiles // []) | index($profile) != null)) | .ctest_name' \
        "$merged_registry" | sort))
if [[ -n "$differences" ]]; then
    echo "CTest and registry names differ:" >&2
    echo "$differences" >&2
    exit 65
fi

printf 'verified %s registered tests against %s\n' \
    "$(jq --arg profile "$profile" \
        '[.tests[] | select((.profiles == null) or ((.profiles // []) | index($profile) != null))] | length' \
        "$merged_registry")" "$build_directory"
