#!/usr/bin/env bash
# Imports the night sky of the environment (stage E3, docs/ENVIRONMENT.md): the star map and the moon of
# SWC_CONTENT/environment/sky (tools/convert-environment.sh sky) and the sky material M_SwcSky into /SwcGameContent/Environment
# (unreal/SoulwardenClient/Scripts/import_sky.py), with the import lock.
# Usage: tools/import-sky.sh [--force] [--grade] [--wait N]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
wait_lock=(-n)
export SWC_SKY="${SWC_CONTENT}/environment/sky/sky.json" SWC_SKY_FORCE=0 SWC_SKY_FORCE_GRADE=0 SWC_SKY_REPORT="${SWC_WORK}/logs/import-sky.json"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --force) SWC_SKY_FORCE=1 ;;
        --grade) SWC_SKY_FORCE_GRADE=1 ;;   # make M_SwcGrade again (the grade of the source and the sun rays), nothing else
        --wait) shift; wait_lock=(-w "$1") ;;
        *) echo "usage: tools/import-sky.sh [--force] [--grade] [--wait N]" >&2; exit 2 ;;
    esac
    shift
done
[[ -f "${SWC_SKY}" ]] || { echo "no ${SWC_SKY}: run tools/convert-environment.sh sky" >&2; exit 1; }
"${SWC_REPO}/tools/content-link.sh"
mkdir -p "${SWC_WORK}/logs"
rm -f "${SWC_SKY_REPORT}"
status=0
# The world lock first (exclusive: the runs load these assets; tools/lib/world-lock.sh, docs/WORLD.md), then import.lock. flock -o:
# the editor does not get the descriptor of the lock (its trace and cache servers live after it).
source "${SWC_REPO}/tools/lib/world-lock.sh"
world_write "${wait_lock[@]}" flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/import_sky.py" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/import-sky.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "import-sky: a run holds the world lock or another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
[[ -f "${SWC_SKY_REPORT}" ]] || { echo "no report: status ${status}; see ${SWC_WORK}/logs/import-sky.log" >&2; exit 1; }
cat "${SWC_SKY_REPORT}"
exit ${status}
