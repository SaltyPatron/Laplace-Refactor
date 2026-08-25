#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 GRAMMAR-SOURCE-ROOT GRAMMAR-LOCK" >&2
    exit 64
fi

source_root=$1
lock_file=$2
if [[ ! -d "$source_root" || ! -f "$lock_file" ]]; then
    echo "grammar source root or lock is missing" >&2
    exit 66
fi
if [[ "$(jq -r '.schema' "$lock_file")" != \
    "laplace.tree-sitter-grammar-lock/v2" ]]; then
    echo "grammar lock schema is unsupported" >&2
    exit 65
fi

locked_count=$(jq '.repository_count' "$lock_file")
actual_count=$(find "$source_root" -mindepth 1 -maxdepth 1 -type d | wc -l)
entry_count=$(jq '.repositories | length' "$lock_file")
if [[ "$locked_count" -ne "$actual_count" || "$entry_count" -ne "$actual_count" ]]; then
    echo "grammar repository count mismatch" >&2
    exit 65
fi

actual_names=$(mktemp)
locked_names=$(mktemp)
cleanup() {
    rm -- "$actual_names" "$locked_names"
}
trap cleanup EXIT
find "$source_root" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort >"$actual_names"
jq -r '.repositories[].name' "$lock_file" | sort >"$locked_names"
if ! cmp -s "$actual_names" "$locked_names"; then
    echo "grammar source and lock names differ" >&2
    exit 65
fi

while IFS= read -r name; do
    source_tree="$source_root/$name"
    revision=$(jq -r --arg name "$name" \
        '.repositories[] | select(.name == $name) | .revision' "$lock_file")
    expected_archive=$(jq -r --arg name "$name" \
        '.repositories[] | select(.name == $name) | .git_archive_sha256' "$lock_file")
    if [[ -n "$(git -C "$source_tree" status --porcelain=v1 --untracked-files=all)" ]]; then
        echo "grammar source contains changes: $name" >&2
        exit 65
    fi
    actual_revision=$(git -C "$source_tree" rev-parse HEAD)
    if [[ "$actual_revision" != "$revision" ]]; then
        echo "grammar revision mismatch: $name" >&2
        exit 65
    fi
    actual_archive=$(git -C "$source_tree" archive --format=tar "$revision" |
        sha256sum | awk '{print $1}')
    if [[ "$actual_archive" != "$expected_archive" ]]; then
        echo "grammar archive mismatch: $name" >&2
        exit 65
    fi
    while IFS=$'\t' read -r license_path expected_license_sha; do
        actual_license_sha=$(sha256sum "$source_tree/$license_path" | awk '{print $1}')
        if [[ "$actual_license_sha" != "$expected_license_sha" ]]; then
            echo "grammar license mismatch: $name/$license_path" >&2
            exit 65
        fi
    done < <(jq -r --arg name "$name" \
        '.repositories[] | select(.name == $name) |
         .license_files[] | [.path, .sha256] | @tsv' "$lock_file")
    while IFS=$'\t' read -r generated_path expected_generated_sha; do
        actual_generated_sha=$(sha256sum "$source_tree/$generated_path" | awk '{print $1}')
        if [[ "$actual_generated_sha" != "$expected_generated_sha" ]]; then
            echo "grammar generated source mismatch: $name/$generated_path" >&2
            exit 65
        fi
    done < <(jq -r --arg name "$name" \
        '.repositories[] | select(.name == $name) |
         (.generated_parsers[]), (.external_scanners[]) |
         [.path, .sha256] | @tsv' "$lock_file")
done <"$locked_names"

printf 'verified %s grammar repositories\n' "$actual_count"
