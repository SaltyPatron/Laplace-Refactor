#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 ]]; then
    echo "usage: $0 LOCK-FILE DESTINATION DEPENDENCY..." >&2
    exit 64
fi

lock_file=$1
destination=$2
shift 2

retry_attempts=${LAPLACE_GIT_FETCH_RETRY_ATTEMPTS:-4}
retry_delay_seconds=${LAPLACE_GIT_FETCH_RETRY_DELAY_SECONDS:-5}

if [[ ! -f "$lock_file" ]]; then
    echo "dependency lock does not exist: $lock_file" >&2
    exit 66
fi
if [[ -e "$destination" ]]; then
    echo "destination already exists: $destination" >&2
    exit 73
fi
if [[ ! "$retry_attempts" =~ ^[1-9][0-9]*$ ]]; then
    echo "LAPLACE_GIT_FETCH_RETRY_ATTEMPTS must be a positive integer" >&2
    exit 64
fi
if [[ ! "$retry_delay_seconds" =~ ^[0-9]+$ ]]; then
    echo "LAPLACE_GIT_FETCH_RETRY_DELAY_SECONDS must be a nonnegative integer" >&2
    exit 64
fi

destination_parent=$(dirname "$destination")
mkdir -p "$destination_parent"
stage=$(mktemp -d "$destination_parent/.locked-git-acquisition.XXXXXXXX")
cleanup_stage() {
    if [[ -d "$stage" ]]; then
        rm -R -- "$stage"
    fi
}
trap cleanup_stage EXIT

declare -A requested=()
for dependency in "$@"; do
    if [[ -n ${requested[$dependency]+present} ]]; then
        echo "dependency requested more than once: $dependency" >&2
        exit 65
    fi
    requested[$dependency]=1

    upstream=$(jq -er --arg dependency "$dependency" \
        '.dependencies[$dependency].upstream | select(type == "string" and length > 0)' \
        "$lock_file")
    revision=$(jq -er --arg dependency "$dependency" \
        '.dependencies[$dependency].revision | select(type == "string" and test("^[0-9a-f]{40}$"))' \
        "$lock_file")
    source="$stage/$dependency"

    git init --quiet "$source"
    git -C "$source" remote add origin "$upstream"

    fetched=false
    for ((attempt = 1; attempt <= retry_attempts; ++attempt)); do
        if git -C "$source" fetch --quiet --depth=1 origin "$revision"; then
            fetched=true
            break
        fi
        if ((attempt < retry_attempts)); then
            printf 'fetch failed for %s at %s (attempt %d/%d); retrying\n' \
                "$dependency" "$revision" "$attempt" "$retry_attempts" >&2
            sleep "$retry_delay_seconds"
        fi
    done
    if [[ "$fetched" != true ]]; then
        echo "unable to fetch locked dependency after $retry_attempts attempts: $dependency@$revision" >&2
        exit 69
    fi

    git -C "$source" checkout --quiet --detach "$revision"
    observed_revision=$(git -C "$source" rev-parse HEAD)
    if [[ "$observed_revision" != "$revision" ]]; then
        echo "locked revision mismatch for $dependency: expected $revision, observed $observed_revision" >&2
        exit 65
    fi
done

mv -- "$stage" "$destination"
trap - EXIT

printf 'acquired %d exact locked Git dependencies in %s\n' "$#" "$destination"
