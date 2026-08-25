#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 RELEASE_ASSET_TOOL TEST_ROOT" >&2
    exit 64
fi

tool=$1
test_root=$2
mkdir -p "$test_root"
fixture=$(mktemp -d "$test_root/release-assets.XXXXXXXX")
cleanup_fixture() {
    if [[ -d "$fixture" ]]; then
        rm -R -- "$fixture"
    fi
}
trap cleanup_fixture EXIT

archive_root="$fixture/archives"
mkdir -p "$archive_root"
archive="$archive_root/pkg-1.0.tar.gz"
python3 - "$archive" <<'PY'
import io
import sys
import tarfile

archive_path = sys.argv[1]
with tarfile.open(archive_path, "w:gz") as output:
    root = tarfile.TarInfo("pkg-1.0")
    root.type = tarfile.DIRTYPE
    root.mode = 0o755
    output.addfile(root)
    source_directory = tarfile.TarInfo("pkg-1.0/src")
    source_directory.type = tarfile.DIRTYPE
    source_directory.mode = 0o755
    output.addfile(source_directory)
    for path, content, mode in (
        ("pkg-1.0/LICENSE", b"license\n", 0o644),
        ("pkg-1.0/src/tool.sh", b"#!/bin/sh\nexit 0\n", 0o755),
    ):
        member = tarfile.TarInfo(path)
        member.size = len(content)
        member.mode = mode
        output.addfile(member, io.BytesIO(content))
    link = tarfile.TarInfo("pkg-1.0/tool-link")
    link.type = tarfile.SYMTYPE
    link.linkname = "src/tool.sh"
    link.mode = 0o777
    output.addfile(link)
PY

"$tool" inspect --archive "$archive" --top-directory pkg-1.0 >"$fixture/metrics.json"
license_sha=$(printf 'license\n' | sha256sum | awk '{print $1}')
jq -n \
    --slurpfile metrics "$fixture/metrics.json" \
    --arg license_sha "$license_sha" \
    '{
      schema: "laplace.release-lock/v1",
      archives: {
        pkg: {
          version: "1.0",
          filename: "pkg-1.0.tar.gz",
          url: "https://example.invalid/pkg-1.0.tar.gz",
          sha256: $metrics[0].archive_sha256,
          size_bytes: $metrics[0].archive_size_bytes,
          top_directory: "pkg-1.0",
          member_count: $metrics[0].member_count,
          regular_file_count: $metrics[0].regular_file_count,
          uncompressed_bytes: $metrics[0].uncompressed_bytes,
          tree_sha256: $metrics[0].tree_sha256,
          licenses: [{path: "pkg-1.0/LICENSE", sha256: $license_sha}]
        }
      }
    }' >"$fixture/lock.json"

"$tool" verify --lock "$fixture/lock.json" --archive-root "$archive_root" \
    >"$fixture/verify.json"
jq -e '.archives | length == 1' "$fixture/verify.json" >/dev/null
"$tool" import \
    --lock "$fixture/lock.json" \
    --archive-root "$archive_root" \
    --destination "$fixture/imported" \
    >"$fixture/import.json"
test -x "$fixture/imported/pkg/src/tool.sh"
test "$(readlink "$fixture/imported/pkg/tool-link")" = "src/tool.sh"
"$tool" verify-import \
    --lock "$fixture/lock.json" \
    --archive-root "$archive_root" \
    --destination "$fixture/imported" \
    >"$fixture/verify-import.json"

set +e
"$tool" import \
    --lock "$fixture/lock.json" \
    --archive-root "$archive_root" \
    --destination "$fixture/imported" >/dev/null 2>&1
existing_status=$?
set -e
test "$existing_status" -eq 1

sed '0,/"schema"/s//"schema": "duplicate",\n  "schema"/' \
    "$fixture/lock.json" >"$fixture/duplicate-lock.json"
set +e
"$tool" verify --lock "$fixture/duplicate-lock.json" --archive-root "$archive_root" \
    >/dev/null 2>&1
duplicate_status=$?
set -e
test "$duplicate_status" -eq 1

printf 'changed\n' >>"$fixture/imported/pkg/src/tool.sh"
set +e
"$tool" verify-import \
    --lock "$fixture/lock.json" \
    --archive-root "$archive_root" \
    --destination "$fixture/imported" >/dev/null 2>&1
imported_tamper_status=$?
set -e
test "$imported_tamper_status" -eq 1

printf 'tamper' >>"$archive"
set +e
"$tool" verify --lock "$fixture/lock.json" --archive-root "$archive_root" \
    >/dev/null 2>&1
tamper_status=$?
set -e
test "$tamper_status" -eq 1

python3 - "$fixture/traversal.tar" <<'PY'
import io
import sys
import tarfile

with tarfile.open(sys.argv[1], "w") as output:
    member = tarfile.TarInfo("pkg-1.0/../../escape")
    member.size = 1
    output.addfile(member, io.BytesIO(b"x"))
PY
set +e
"$tool" inspect --archive "$fixture/traversal.tar" --top-directory pkg-1.0 \
    >/dev/null 2>&1
traversal_status=$?
set -e
test "$traversal_status" -eq 1
