#!/usr/bin/env bash
# Imports the cookies of the local lights (stage E2, docs/ENVIRONMENT.md): the projector textures of
# SWC_CONTENT/environment/cookies.json (tools/convert-environment.sh cookies), the light function material, and one material instance
# for each texture, into /SwcGameContent/Environment (unreal/SoulwardenClient/Scripts/import_cookies.py), with the import lock.
# Usage: tools/import-cookies.sh [--force] [--wait N]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
wait_lock=(-n)
export SWC_COOKIES="${SWC_CONTENT}/environment/cookies.json" SWC_COOKIES_FORCE=0 SWC_COOKIES_REPORT="${SWC_WORK}/logs/import-cookies.json"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --force) SWC_COOKIES_FORCE=1 ;;
        --wait) shift; wait_lock=(-w "$1") ;;
        *) echo "usage: tools/import-cookies.sh [--force] [--wait N]" >&2; exit 2 ;;
    esac
    shift
done
[[ -f "${SWC_COOKIES}" ]] || { echo "no ${SWC_COOKIES}: run tools/convert-environment.sh cookies" >&2; exit 1; }
"${SWC_REPO}/tools/content-link.sh"
mkdir -p "${SWC_WORK}/logs"
rm -f "${SWC_COOKIES_REPORT}"
status=0
# The world lock first (exclusive: the runs load these assets; tools/lib/world-lock.sh, docs/WORLD.md), then import.lock. flock -o:
# the editor does not get the descriptor of the lock (its trace and cache servers live after it).
source "${SWC_REPO}/tools/lib/world-lock.sh"
world_write "${wait_lock[@]}" flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/import_cookies.py" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/import-cookies.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "import-cookies: a run holds the world lock or another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
[[ -f "${SWC_COOKIES_REPORT}" ]] || { echo "no report: status ${status}; see ${SWC_WORK}/logs/import-cookies.log" >&2; exit 1; }
cat "${SWC_COOKIES_REPORT}"
exit ${status}
