#!/usr/bin/env bash
# Stops each process of the development runs of this project: the processes with the mark SWC_DEV_RUN=1 in their
# environment (tools/dev-run.sh). It does not stop a player run, the retail game, or a Soulwarden development run.
# The backend saves its state at SIGTERM. Usage: tools/dev-stop.sh [--check]   (--check: status 1 while one runs)
set -euo pipefail
mark="SWC_DEV_RUN=1"

marked() {
    local file
    for file in $(grep -lzxF -- "${mark}" /proc/[0-9]*/environ 2>/dev/null || true); do
        file="${file#/proc/}"
        echo "${file%/environ}"
    done
}

if [[ "${1:-}" == "--check" ]]; then
    processes="$(marked)"
    if [[ -n "${processes}" ]]; then
        echo "dev-stop: development run processes: $(echo ${processes})" >&2
        exit 1
    fi
    exit 0
fi
processes="$(marked)"
[[ -z "${processes}" ]] && exit 0
kill ${processes} 2>/dev/null || true
for ((attempt = 0; attempt < 150; ++attempt)); do
    [[ -z "$(marked)" ]] && exit 0
    sleep 0.1
done
processes="$(marked)"
[[ -n "${processes}" ]] && kill -9 ${processes} 2>/dev/null || true
echo "dev-stop: stopped" >&2
