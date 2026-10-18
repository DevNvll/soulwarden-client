#!/usr/bin/env bash
# Stops the processes of the development runs of this project on the own data folder SWC_DEV_DATA: the processes with
# the marks SWC_DEV_RUN=1 and SWC_SESSION_DATA=SWC_DEV_DATA in their environment (tools/dev-run.sh). The runs of the
# other tracks (an other SWC_DEV_DATA) continue. It does not stop a player run, the retail game, or a Soulwarden
# development run. The backend saves its state at SIGTERM.
# Usage: tools/dev-stop.sh [--all] [--check]   (--all: the runs of all tracks; --check: status 1 while one runs)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
mark="SWC_DEV_RUN=1"
all=0
check=0
for argument in "$@"; do
    case "${argument}" in
        --all) all=1 ;;
        --check) check=1 ;;
        *) echo "dev-stop: unknown option ${argument}" >&2; exit 2 ;;
    esac
done
# dev-run.sh writes SWC_DEV_DATA as it is and as its real path: both match.
data_marks=(-e "SWC_SESSION_DATA=${SWC_DEV_DATA}" -e "SWC_SESSION_DATA=$(realpath -m -- "${SWC_DEV_DATA}")")

marked() {
    local file
    for file in $(grep -lzxF -- "${mark}" /proc/[0-9]*/environ 2>/dev/null || true); do
        if [[ "${all}" == "0" ]] && ! grep -qzxF "${data_marks[@]}" "${file}" 2>/dev/null; then
            continue
        fi
        file="${file#/proc/}"
        echo "${file%/environ}"
    done
}

if [[ "${check}" == "1" ]]; then
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
