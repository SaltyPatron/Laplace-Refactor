#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 GRAMMAR-SOURCE-ROOT OUTPUT-LOCK" >&2
    exit 64
fi

source_root=$1
output_lock=$2
if [[ ! -d "$source_root" ]]; then
    echo "grammar source root does not exist: $source_root" >&2
    exit 66
fi
if [[ -e "$output_lock" ]]; then
    echo "grammar lock already exists: $output_lock" >&2
    exit 73
fi

output_parent=$(dirname "$output_lock")
mkdir -p "$output_parent"
records=$(mktemp "$output_parent/.grammar-records.XXXXXXXX")
result=$(mktemp "$output_parent/.grammar-lock.XXXXXXXX")
cleanup() {
    if [[ -f "$records" ]]; then
        rm -- "$records"
    fi
    if [[ -f "$result" ]]; then
        rm -- "$result"
    fi
}
trap cleanup EXIT

while IFS= read -r source_tree; do
    name=$(basename "$source_tree")
    if [[ ! -e "$source_tree/.git" ]]; then
        echo "grammar is not a Git source tree: $source_tree" >&2
        exit 65
    fi
    if [[ -n "$(git -C "$source_tree" status --porcelain=v1 --untracked-files=all)" ]]; then
        echo "grammar source contains changes: $source_tree" >&2
        exit 65
    fi
    revision=$(git -C "$source_tree" rev-parse HEAD)
    archive_sha=$(git -C "$source_tree" archive --format=tar "$revision" |
        sha256sum | awk '{print $1}')
    upstream=$(git -C "$source_tree" remote get-url origin)
    description=$(git -C "$source_tree" describe --tags --always --dirty)

    licenses='[]'
    while IFS= read -r license_file; do
        relative_path=${license_file#"$source_tree/"}
        license_sha=$(sha256sum "$license_file" | awk '{print $1}')
        licenses=$(jq -c \
            --arg path "$relative_path" \
            --arg sha256 "$license_sha" \
            '. + [{path: $path, sha256: $sha256}]' <<<"$licenses")
    done < <(find "$source_tree" -maxdepth 1 -type f \
        \( -iname 'license*' -o -iname 'copying*' \) | sort)

    declared_license=""
    if [[ -f "$source_tree/package.json" ]]; then
        declared_license=$(jq -r '.license // empty' "$source_tree/package.json")
    fi
    if [[ -z "$declared_license" && -f "$source_tree/Cargo.toml" ]]; then
        declared_license=$(sed -n \
            's/^[[:space:]]*license[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' \
            "$source_tree/Cargo.toml" | head -n 1)
    fi

    tree_sitter_json_sha=""
    if [[ -f "$source_tree/tree-sitter.json" ]]; then
        tree_sitter_json_sha=$(sha256sum "$source_tree/tree-sitter.json" | awk '{print $1}')
    fi
    generated_parsers='[]'
    while IFS= read -r parser_file; do
        relative_path=${parser_file#"$source_tree/"}
        parser_sha=$(sha256sum "$parser_file" | awk '{print $1}')
        generated_parsers=$(jq -c \
            --arg path "$relative_path" \
            --arg sha256 "$parser_sha" \
            '. + [{path: $path, sha256: $sha256}]' <<<"$generated_parsers")
    done < <(find "$source_tree" -type f -path '*/src/parser.c' | sort)

    external_scanners='[]'
    while IFS= read -r scanner_file; do
        relative_path=${scanner_file#"$source_tree/"}
        scanner_sha=$(sha256sum "$scanner_file" | awk '{print $1}')
        external_scanners=$(jq -c \
            --arg path "$relative_path" \
            --arg sha256 "$scanner_sha" \
            '. + [{path: $path, sha256: $sha256}]' <<<"$external_scanners")
    done < <(find "$source_tree" -type f \
        \( -path '*/src/scanner.c' -o -path '*/src/scanner.cc' \
           -o -path '*/src/scanner.cpp' \) | sort)
    license_count=$(jq 'length' <<<"$licenses")
    if [[ "$license_count" -gt 0 ]]; then
        packaging_eligible=true
    else
        packaging_eligible=false
    fi

    jq -cn \
        --arg name "$name" \
        --arg upstream "$upstream" \
        --arg revision "$revision" \
        --arg git_archive_sha256 "$archive_sha" \
        --arg description "$description" \
        --arg declared_license "$declared_license" \
        --arg tree_sitter_json_sha256 "$tree_sitter_json_sha" \
        --argjson licenses "$licenses" \
        --argjson generated_parsers "$generated_parsers" \
        --argjson external_scanners "$external_scanners" \
        --argjson packaging_eligible "$packaging_eligible" \
        '{
            name: $name,
            upstream: $upstream,
            revision: $revision,
            git_archive_sha256: $git_archive_sha256,
            description: $description,
            declared_license: $declared_license,
            license_files: $licenses,
            tree_sitter_json_sha256: $tree_sitter_json_sha256,
            generated_parsers: $generated_parsers,
            external_scanners: $external_scanners,
            packaging_eligible: $packaging_eligible
        }' >>"$records"
done < <(find "$source_root" -mindepth 1 -maxdepth 1 -type d | sort)

jq -s '{
    schema: "laplace.tree-sitter-grammar-lock/v2",
    source_kind: "independent-upstream-git-repositories",
    repository_count: length,
    repositories: sort_by(.name)
}' "$records" >"$result"
mv -- "$result" "$output_lock"
trap - EXIT
rm -- "$records"

printf 'wrote grammar lock for %s repositories to %s\n' \
    "$(jq '.repository_count' "$output_lock")" "$output_lock"
