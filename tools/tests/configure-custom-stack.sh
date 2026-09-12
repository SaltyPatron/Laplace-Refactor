#!/usr/bin/env bash
set -euo pipefail
c_flags=$(jq -r '.build.c_flags | join(" ")' contracts/postgresql-build.json)
cxx_flags=$(jq -r '.build.cxx_flags | join(" ")' contracts/postgresql-build.json)
linker_flags=$(jq -r '.build.linker_flags | join(" ")' contracts/postgresql-build.json)
mountpoint -q /build || { echo 'Required build volume is not mounted' >&2; exit 1; }
umask 0002
export TMPDIR=/build/laplace/work/refactor-scratch TMP=/build/laplace/work/refactor-scratch TEMP=/build/laplace/work/refactor-scratch
mkdir -p "$TMPDIR"
# Key the persistent build by the actual recipe, tools, dependency locks and paths.
# Source edits stay in this tree so Ninja rebuilds only their dependent targets.
configuration=$({
  sha256sum "$0" "$LAPLACE_ICX" "$LAPLACE_ICPX" "$LAPLACE_PG_CONFIG" "$(command -v cmake)" "$(command -v ninja)" dependencies/lock.json dependencies/installed-lock.json
  printf '%s\n' "$PWD" "$c_flags" "$cxx_flags" "$linker_flags" "$LAPLACE_DEPENDENCY_ROOT" "$LAPLACE_SOURCE_ESTATE_ROOT" "$LAPLACE_UNICODE_SOURCE_ROOT" "$LAPLACE_ISO_639_SOURCE_ROOT" "$LAPLACE_CILI_SOURCE_ROOT"
  "$LAPLACE_PG_CONFIG" --configure
} | sha256sum | cut -d ' ' -f 1)
build_directory="/build/laplace/build/refactor-custom-stack/$configuration"
mkdir -p "$build_directory"
if [[ -O "$build_directory" ]]; then chmod 2770 "$build_directory"; fi
printf 'Native configuration fingerprint: %s\nPersistent build: %s\n' "$configuration" "$build_directory"
cmake -S . -B "$build_directory" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER="$LAPLACE_ICX" \
  -DCMAKE_CXX_COMPILER="$LAPLACE_ICPX" \
  -DCMAKE_C_FLAGS_RELWITHDEBINFO="$c_flags" \
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="$cxx_flags" \
  -DCMAKE_EXE_LINKER_FLAGS="$linker_flags" \
  -DLAPLACE_BLAKE3_SOURCE="$LAPLACE_DEPENDENCY_ROOT/blake3/c" \
  -DLAPLACE_GTEST_SOURCE="$LAPLACE_DEPENDENCY_ROOT/googletest" \
  -DLAPLACE_EIGEN_SOURCE="$LAPLACE_DEPENDENCY_ROOT/eigen" \
  -DLAPLACE_SPECTRA_SOURCE="$LAPLACE_DEPENDENCY_ROOT/spectra" \
  -DLAPLACE_TREE_SITTER_SOURCE="$LAPLACE_DEPENDENCY_ROOT/tree-sitter" \
  -DLAPLACE_SOURCE_ESTATE_ROOT="$LAPLACE_SOURCE_ESTATE_ROOT" \
  -DLAPLACE_UNICODE_SOURCE_ROOT="$LAPLACE_UNICODE_SOURCE_ROOT" \
  -DLAPLACE_ISO_639_SOURCE_ROOT="$LAPLACE_ISO_639_SOURCE_ROOT" \
  -DLAPLACE_CILI_SOURCE_ROOT="$LAPLACE_CILI_SOURCE_ROOT" \
  -DLAPLACE_ENABLE_DOTNET_BINDINGS=ON \
  -DLAPLACE_VERIFY_ONEAPI_INSTALLED_PROVIDER=ON \
  -DLAPLACE_PG_CONFIG="$LAPLACE_PG_CONFIG"
printf 'LAPLACE_CI_BUILD_DIRECTORY=%s\n' "$build_directory" >> "$GITHUB_ENV"
