#!/usr/bin/env bash
# Repair only the configured shared source-cache parents, preserving every UID.
set -euo pipefail
umask 0002

repair_source_parent() {
    local path=$1 group=$2
    if [[ -L "$path" || ( -e "$path" && ! -d "$path" ) ]]; then
        echo "Expected a physical source-cache directory: $path" >&2
        return 65
    fi
    if [[ ! -d "$path" ]]; then
        if [[ -w $(dirname -- "$path") ]]; then
            install -d -g "$group" -m 2775 "$path"
        elif ! sudo -n install -d -g "$group" -m 2775 "$path"; then
            echo "Required host command: sudo install -d -g $group -m 2775 $path" >&2
            stat -c 'Existing parent: %n owner=%U group=%G mode=%a' "$(dirname -- "$path")" >&2
            return 73
        fi
    fi
    local previous_uid
    previous_uid=$(stat -c '%u' "$path")
    if [[ $(stat -c '%G' "$path") != "$group" ]]; then
        if [[ -O "$path" ]]; then
            chgrp "$group" "$path"
        elif ! sudo -n chgrp "$group" "$path"; then
            echo "Required host command: sudo chgrp $group $path" >&2
            stat -c 'Source parent: %n owner=%U group=%G mode=%a' "$path" >&2
            return 73
        fi
    fi
    if [[ ! -g "$path" || ! -w "$path" || ! $(stat -c '%A' "$path") =~ ^....rw[xs] ]]; then
        if [[ -O "$path" ]]; then
            chmod g+rwx,g+s "$path"
        elif ! sudo -n chmod g+rwx,g+s "$path"; then
            echo "Required host command: sudo chmod g+rwx,g+s $path" >&2
            stat -c 'Source parent: %n owner=%U group=%G mode=%a' "$path" >&2
            return 73
        fi
    fi
    [[ $(stat -c '%u' "$path") == "$previous_uid" && -w "$path" && -g "$path" ]] || {
        echo "Source parent repair did not preserve its owner and shared access: $path" >&2
        return 73
    }
}

repair_source_lock() {
    local path=$1 group=$2
    [[ -e "$path" || -L "$path" ]] || return 0
    [[ -f "$path" && ! -L "$path" ]] || {
        echo "Expected a physical source-acquisition lock file: $path" >&2
        return 65
    }
    local identity
    identity=$(stat -c '%d:%i:%u' "$path")
    if [[ $(stat -c '%G' "$path") != "$group" ]]; then
        if [[ -O "$path" ]]; then
            chgrp "$group" "$path"
        elif ! sudo -n chgrp "$group" "$path"; then
            echo "Required host command: sudo chgrp $group $path" >&2
            stat -c 'Source lock: %n owner=%U group=%G mode=%a' "$path" >&2
            return 73
        fi
    fi
    if [[ ! -w "$path" || ! $(stat -c '%A' "$path") =~ ^....rw ]]; then
        if [[ -O "$path" ]]; then
            chmod g+rw "$path"
        elif ! sudo -n chmod g+rw "$path"; then
            echo "Required host command: sudo chmod g+rw $path" >&2
            stat -c 'Source lock: %n owner=%U group=%G mode=%a' "$path" >&2
            return 73
        fi
    fi
    [[ $(stat -c '%d:%i:%u' "$path") == "$identity" && -w "$path" ]] || {
        echo "Source lock repair did not preserve its inode, owner and shared access: $path" >&2
        return 73
    }
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
    [[ $# == 0 ]] || { echo 'prepare-source-parents.sh takes no path arguments' >&2; exit 64; }
    for path in /opt/laplace/external /opt/laplace/external/source-generations; do
        repair_source_parent "$path" laplace-runner
    done
    repair_source_lock /opt/laplace/external/source-generations/.source-acquisition.lock laplace-runner
fi
