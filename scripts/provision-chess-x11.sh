#!/usr/bin/env bash
# Runtime libraries and observable input/window tools for the selected Qt GUI.
set -euo pipefail
packages=(
    xvfb xauth xdotool x11-utils fonts-dejavu-core
    libx11-xcb1 libxcb1 libxcb-cursor0 libxcb-icccm4 libxcb-image0
    libxcb-keysyms1 libxcb-render-util0 libxcb-render0 libxcb-randr0
    libxcb-shape0 libxcb-shm0 libxcb-sync1 libxcb-xfixes0 libxcb-xinerama0
    libxcb-xinput0 libxcb-xkb1 libxcb-glx0 libxkbcommon-x11-0
    libxrender1 libxext6 libsm6 libice6 libfontconfig1 libfreetype6
)
command -v apt-get >/dev/null && command -v dpkg-query >/dev/null || {
    echo 'Virtual X11 host provisioning requires the existing Debian/Ubuntu package owner.' >&2
    exit 1
}
missing=()
for package in "${packages[@]}"; do
    status=$(dpkg-query -W -f='${db:Status-Status}' "$package" 2>/dev/null || true)
    [[ "$status" == installed ]] || missing+=("$package")
done
if ((${#missing[@]})); then
    if ((EUID != 0)); then
        exec sudo -n bash "$0"
    fi
    apt_options=(-o DPkg::Lock::Timeout=30 -o Acquire::Retries=2
                 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30)
    apt-get "${apt_options[@]}" update
    DEBIAN_FRONTEND=noninteractive apt-get "${apt_options[@]}" install --yes --no-install-recommends "${missing[@]}"
fi
dpkg-query -W -f='${binary:Package}\t${Version}\n' "${packages[@]}"
