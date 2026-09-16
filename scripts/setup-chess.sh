#!/usr/bin/env bash
# Build the selected upstream chess sources in the existing dependency estate.
set -euo pipefail
umask 0002
repository=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export TMPDIR=/build/laplace/work TMP=/build/laplace/work TEMP=/build/laplace/work
[[ -d "$TMPDIR" ]] || { echo "Required build scratch is missing: $TMPDIR" >&2; exit 1; }

# GUI selection also provisions the X11 runtime used by actual window acceptance.
for argument in "$@"; do
    if [[ "$argument" == --cutechess-gui ]]; then
        timeout --signal=TERM --kill-after=10s 300s bash "$repository/scripts/provision-chess-x11.sh"
        break
    fi
done

if [[ $EUID == 0 ]]; then
    command -v apt-get >/dev/null || { echo 'Install a C++17 compiler, make, CMake >=3.20, Python venv, and Qt platform libraries for this OS first.' >&2; exit 1; }
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install --yes \
        build-essential cmake ninja-build git jq ca-certificates curl python3-venv \
        libgl-dev libegl-dev libopengl-dev libxkbcommon-dev libxcb-cursor0 \
        libfontconfig1 libdbus-1-3
    if id laplace-runner >/dev/null 2>&1; then
        bash "$repository/tools/dependencies/prepare-source-parents.sh"
        for path in /opt/laplace/tools /opt/laplace/tools/chess /build/laplace/build/chess; do
            [[ ! -L "$path" ]] || { echo "Expected physical dependency parent: $path" >&2; exit 1; }
            existing_owner=0
            [[ ! -e "$path" ]] || existing_owner=$(stat -c '%u' "$path")
            install -d -o "$existing_owner" -g laplace-runner -m 2775 "$path"
        done
        environment=("TMPDIR=$TMPDIR" "TMP=$TMP" "TEMP=$TEMP")
        for name in LAPLACE_VERIFIED_SOURCE_ROOT LAPLACE_STOCKFISH_SOURCE LAPLACE_BUILD_JOBS CMAKE_BUILD_PARALLEL_LEVEL LAPLACE_QT_PREFIX QT_ROOT_DIR CMAKE_PREFIX_PATH; do
            if [[ -n ${!name:-} ]]; then
                environment+=("$name=${!name}")
            fi
        done
        exec sudo -u laplace-runner -H -- env "${environment[@]}" \
            bash "$repository/scripts/setup-chess.sh" "$@"
    fi
fi

exec python3 "$repository/tools/dependencies/chess_tools.py" install --profile "$@"
