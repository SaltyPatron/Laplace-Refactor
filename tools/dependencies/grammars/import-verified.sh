#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 GRAMMAR-ASSET-ROOT DESTINATION" >&2
    exit 64
fi

asset_root=$1
destination=$2
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
lock_file="$repo_root/dependencies/tree-sitter-grammars.lock.json"
verify_script="$repo_root/tools/dependencies/grammars/verify-lock.sh"

if [[ ! -d "$asset_root" ]]; then
    echo "grammar asset root does not exist: $asset_root" >&2
    exit 66
fi
if [[ -e "$destination" ]]; then
    echo "grammar destination already exists: $destination" >&2
    exit 73
fi

"$verify_script" "$asset_root" "$lock_file"

destination_parent=$(dirname "$destination")
mkdir -p "$destination_parent"
stage=$(mktemp -d "$destination_parent/.grammar-import.XXXXXXXX")
cleanup_stage() {
    if [[ -d "$stage" ]]; then
        rm -R -- "$stage"
    fi
}
trap cleanup_stage EXIT

while IFS=$'\t' read -r name revision; do
    git clone --quiet --no-hardlinks --no-checkout \
        "$asset_root/$name" "$stage/$name"
    mkdir -p "$stage/$name/.git/info"
    printf '* -text\n' >"$stage/$name/.git/info/attributes"
    git -C "$stage/$name" checkout --quiet --detach "$revision"
done < <(jq -r '.repositories[] | [.name, .revision] | @tsv' "$lock_file")

"$verify_script" "$stage" "$lock_file"
mv -- "$stage" "$destination"
trap - EXIT

printf 'imported %s verified grammar repositories to %s\n' \
    "$(jq '.repository_count' "$lock_file")" "$destination"
