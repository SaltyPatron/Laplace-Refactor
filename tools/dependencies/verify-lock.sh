#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 SOURCE-ROOT" >&2
    exit 64
fi

source_root=$1
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
lock_file="$repo_root/dependencies/lock.json"

if [[ ! -d "$source_root" ]]; then
    echo "dependency source root does not exist: $source_root" >&2
    exit 66
fi

mapfile -t dependencies < <(jq -r '.dependencies | keys[]' "$lock_file")
if [[ ${#dependencies[@]} -eq 0 ]]; then
    echo "dependency lock contains no entries: $lock_file" >&2
    exit 65
fi

for dependency in "${dependencies[@]}"; do
    source_tree=$(realpath -e "$source_root/$dependency")
    revision=$(jq -r --arg dependency "$dependency" \
        '.dependencies[$dependency].revision' "$lock_file")
    expected_archive=$(jq -r --arg dependency "$dependency" \
        '.dependencies[$dependency].git_archive_sha256' "$lock_file")

    if [[ ! -e "$source_tree/.git" ]]; then
        echo "$dependency source is not a Git upstream tree: $source_tree" >&2
        exit 65
    fi
    git_source=(git -c "safe.directory=$source_tree" -C "$source_tree")
    if [[ -n "$("${git_source[@]}" status --porcelain=v1 --untracked-files=all)" ]]; then
        echo "$dependency source contains changes: $source_tree" >&2
        exit 65
    fi
    actual_revision=$("${git_source[@]}" rev-parse HEAD)
    if [[ "$actual_revision" != "$revision" ]]; then
        echo "$dependency revision mismatch: $actual_revision != $revision" >&2
        exit 65
    fi
    actual_archive=$("${git_source[@]}" archive --format=tar "$revision" | sha256sum | awk '{print $1}')
    if [[ "$actual_archive" != "$expected_archive" ]]; then
        echo "$dependency source archive mismatch: $actual_archive != $expected_archive" >&2
        exit 65
    fi

    while IFS=$'\t' read -r license_path expected_license_sha; do
        source_license="$source_tree/$license_path"
        if [[ ! -f "$source_license" ]]; then
            echo "$dependency license file is missing: $license_path" >&2
            exit 65
        fi
        actual_license_sha=$(sha256sum "$source_license" | awk '{print $1}')
        if [[ "$actual_license_sha" != "$expected_license_sha" ]]; then
            echo "$dependency license mismatch for $license_path: $actual_license_sha != $expected_license_sha" >&2
            exit 65
        fi
    done < <(jq -r --arg dependency "$dependency" \
        '.dependencies[$dependency].licenses[] | [.path, .sha256] | @tsv' \
        "$lock_file")
done

printf 'verified %s dependency source trees at %s\n' \
    "${#dependencies[@]}" "$source_root"
