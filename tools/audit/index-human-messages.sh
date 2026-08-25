#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 codex SESSION.jsonl | claude PARENT.jsonl [...]" >&2
    exit 64
}

[[ $# -ge 2 ]] || usage

source_format=$1
shift

emit_record() {
    local source_file=$1
    local timestamp=$2
    local message_id=$3
    local session_id=$4
    local branch=$5
    local block_count=$6
    local encoded_text=$7
    local digest
    local branch_digest=""
    local byte_count

    digest=$(printf '%s' "$encoded_text" | base64 --decode | sha256sum | awk '{print $1}')
    byte_count=$(printf '%s' "$encoded_text" | base64 --decode | wc -c)
    byte_count=${byte_count//[[:space:]]/}
    [[ "$session_id" == "-" ]] && session_id=""
    [[ "$branch" == "-" ]] && branch=""
    if [[ -n "$branch" ]]; then
        branch_digest=$(printf '%s' "$branch" | sha256sum | awk '{print $1}')
    fi

    jq -nc \
        --arg source_format "$source_format" \
        --arg source_file "$source_file" \
        --arg timestamp "$timestamp" \
        --arg message_id "$message_id" \
        --arg session_id "$session_id" \
        --arg branch_sha256 "$branch_digest" \
        --argjson branch_present "$([[ -n "$branch" ]] && echo true || echo false)" \
        --arg content_sha256 "$digest" \
        --argjson content_bytes "$byte_count" \
        --argjson text_block_count "$block_count" \
        '{
            schema: "laplace.human-message-evidence/v1",
            source_format: $source_format,
            source_file: $source_file,
            timestamp: $timestamp,
            message_id: $message_id,
            session_id: $session_id,
            branch_present: $branch_present,
            branch_sha256: $branch_sha256,
            text_block_count: $text_block_count,
            content_bytes: $content_bytes,
            content_sha256: $content_sha256
        }'
}

case "$source_format" in
    codex)
        [[ $# -eq 1 ]] || usage
        source_path=$1
        source_file=$(basename "$source_path")
        source_session=${source_file%.jsonl}

        while IFS=$'\t' read -r timestamp message_id session_id branch block_count encoded_text; do
            emit_record "$source_file" "$timestamp" "$message_id" "$session_id" "$branch" "$block_count" "$encoded_text"
        done < <(
            jq -r --arg source_session "$source_session" '
                select(
                    .type == "response_item"
                    and .payload.type == "message"
                    and .payload.role == "user"
                )
                | [.payload.content[] | select(.type == "input_text") | .text] as $blocks
                | select(($blocks | length) > 0)
                | [
                    .timestamp,
                    .payload.id,
                    $source_session,
                    "-",
                    ($blocks | length),
                    (($blocks | join("\n")) | @base64)
                ]
                | @tsv
            ' "$source_path"
        )
        ;;
    claude)
        for source_path in "$@"; do
            source_file=$(basename "$source_path")

            while IFS=$'\t' read -r timestamp message_id session_id branch block_count encoded_text; do
                emit_record "$source_file" "$timestamp" "$message_id" "$session_id" "$branch" "$block_count" "$encoded_text"
            done < <(
                jq -r '
                    def text_blocks:
                        if (.message.content | type) == "string" then
                            [.message.content]
                        elif (.message.content | type) == "array" then
                            [.message.content[] | select(.type == "text") | .text]
                        else
                            []
                        end;
                    select(.type == "user" and .message.role == "user")
                    | text_blocks as $blocks
                    | select(($blocks | length) > 0)
                    | ($blocks | join("\n")) as $text
                    | select(
                        ($text | startswith("<task-notification>")) | not
                        and (($text | startswith("<local-command")) | not)
                        and (($text | startswith("<command-")) | not)
                        and (($text | startswith("[Request interrupted")) | not)
                    )
                    | [
                        .timestamp,
                        .uuid,
                        (.sessionId // ""),
                        (.gitBranch // ""),
                        ($blocks | length),
                        ($text | @base64)
                    ]
                    | @tsv
                ' "$source_path"
            )
        done
        ;;
    *)
        usage
        ;;
esac
