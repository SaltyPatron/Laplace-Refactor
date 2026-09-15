#!/usr/bin/env bash
# Complete a source generation with the existing exact-commit acquisition owner.
set -euo pipefail
umask 0002
[[ $# == 1 ]] || { echo "usage: $0 SOURCE-ROOT" >&2; exit 64; }
destination=$1
repository=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
lock="$repository/dependencies/lock.json"
parent=$(dirname -- "$destination")
mkdir -p "$parent"
# Serialize publication; existing source content and ownership remain untouched.
exec 9>"$parent/.source-acquisition.lock"
flock 9
[[ ! -L "$destination" ]] || { echo "Source generation must be a physical directory: $destination" >&2; exit 65; }
mkdir -p "$destination"
missing=()
while IFS= read -r dependency; do
    if [[ -e "$destination/$dependency" ]]; then
        "$repository/tools/dependencies/verify-lock.sh" "$destination" "$dependency"
    else
        missing+=("$dependency")
    fi
done < <(jq -r '.dependencies | keys[]' "$lock")
if [[ ${#missing[@]} -gt 0 ]]; then
    work=$(mktemp -d "$parent/.source-generation-fill.XXXXXXXX")
    trap 'rm -rf -- "$work"' EXIT
    "$repository/tools/dependencies/acquire-locked-git.sh" "$lock" "$work/sources" "${missing[@]}"
    "$repository/tools/dependencies/verify-lock.sh" "$work/sources" "${missing[@]}"
    for dependency in "${missing[@]}"; do
        mv -- "$work/sources/$dependency" "$destination/$dependency"
    done
fi
"$repository/tools/dependencies/verify-lock.sh" "$destination"
