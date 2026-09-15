#!/usr/bin/env bash
# Complete a source generation with the existing exact-commit acquisition owner.
set -euo pipefail
umask 0002
[[ $# == 1 ]] || { echo "usage: $0 SOURCE-ROOT" >&2; exit 64; }
destination=$1
repository=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
lock="$repository/dependencies/lock.json"
parent=$(dirname -- "$destination")
[[ ! -L "$destination" ]] || { echo "Source generation must be a physical directory: $destination" >&2; exit 65; }
[[ ! -L "$parent" ]] || { echo "Source generation parent must be a physical directory: $parent" >&2; exit 65; }
# An already-complete immutable generation needs no publication lock or writes.
complete=true
while IFS= read -r dependency; do
    [[ -e "$destination/$dependency" ]] || complete=false
done < <(jq -r '.dependencies | keys[]' "$lock")
if [[ "$complete" == true ]]; then
    "$repository/tools/dependencies/verify-lock.sh" "$destination"
    exit 0
fi
if [[ "$parent" == /opt/laplace/external/source-generations ]]; then
    bash "$repository/tools/dependencies/prepare-source-parents.sh"
fi
mkdir -p "$parent"
if [[ ! -w "$parent" ]]; then
    stat -c 'Source generation parent: %n owner=%U group=%G mode=%a' "$parent" >&2
    echo 'The configured source-generation parent needs group write/setgid repair through scripts/setup-host.sh storage before acquiring a new generation.' >&2
    exit 73
fi
# Serialize publication; existing source content and ownership remain untouched.
[[ ! -L "$parent/.source-acquisition.lock" ]] || { echo "Source publication lock must not be a symlink: $parent/.source-acquisition.lock" >&2; exit 65; }
exec 9>>"$parent/.source-acquisition.lock"
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
