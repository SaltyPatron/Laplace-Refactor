#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 UPSTREAM-ASSET-ROOT DESTINATION" >&2
    exit 64
fi

asset_root=$1
destination=$2
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
lock_file="$repo_root/dependencies/lock.json"
verify_script="$repo_root/tools/dependencies/verify-lock.sh"

if [[ ! -d "$asset_root" ]]; then
    echo "upstream asset root does not exist: $asset_root" >&2
    exit 66
fi
if [[ -e "$destination" ]]; then
    echo "destination already exists: $destination" >&2
    exit 73
fi

"$verify_script" "$asset_root"

destination_parent=$(dirname "$destination")
mkdir -p "$destination_parent"
stage=$(mktemp -d "$destination_parent/.dependency-import.XXXXXXXX")
cleanup_stage() {
    if [[ -d "$stage" ]]; then
        rm -R -- "$stage"
    fi
}
trap cleanup_stage EXIT

mapfile -t dependencies < <(jq -r '.dependencies | keys[]' "$lock_file")
if [[ ${#dependencies[@]} -eq 0 ]]; then
    echo "dependency lock contains no entries: $lock_file" >&2
    exit 65
fi

for dependency in "${dependencies[@]}"; do
    source_tree="$asset_root/$dependency"
    revision=$(jq -r --arg dependency "$dependency" \
        '.dependencies[$dependency].revision' "$lock_file")
    imported_tree="$stage/$dependency"

    git clone --quiet --no-hardlinks --no-checkout "$source_tree" "$imported_tree"
    git -C "$imported_tree" checkout --quiet --detach "$revision"
done

"$verify_script" "$stage"

mv -- "$stage" "$destination"
trap - EXIT

printf 'verified dependency sources imported to %s\n' "$destination"
