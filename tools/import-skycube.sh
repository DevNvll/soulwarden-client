#!/usr/bin/env bash
# Imports the source cloud layers of the water sky cube (AST-200 part 5, docs/WATER.md "The water sky cube"): SWC_CONTENT/water/skycube.json
# (python3 -B -m swc_convert.skycube write): the dome layer meshes, the mask textures, the parent M_SwcDistanceClouds, and one instance
# for each layer, all new in /SwcGameContent/Water/SkyCube (unreal/SoulwardenClient/Scripts/import_skycube.py), with the world lock and the import lock.
# Usage: tools/import-skycube.sh [--force] [--wait N]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
wait_lock=(-n)
export SWC_SKYCUBE="${SWC_CONTENT}/water/skycube.json" SWC_SKYCUBE_FORCE=0 SWC_SKYCUBE_REPORT="${SWC_WORK}/logs/import-skycube.json"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --force) SWC_SKYCUBE_FORCE=1 ;;
        --wait) shift; wait_lock=(-w "$1") ;;
        *) echo "usage: tools/import-skycube.sh [--force] [--wait N]" >&2; exit 2 ;;
    esac
    shift
done
[[ -f "${SWC_SKYCUBE}" ]] || { echo "no ${SWC_SKYCUBE}: run python3 -B -m swc_convert.skycube write" >&2; exit 1; }
"${SWC_REPO}/tools/content-link.sh"
mkdir -p "${SWC_WORK}/logs"
rm -f "${SWC_SKYCUBE_REPORT}"
status=0
# The world lock first (exclusive: the runs load these assets; tools/lib/world-lock.sh, docs/WORLD.md), then import.lock. flock -o:
# the editor does not get the descriptor of the lock (its trace and cache servers live after it).
source "${SWC_REPO}/tools/lib/world-lock.sh"
world_write "${wait_lock[@]}" flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/import_skycube.py" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/import-skycube.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "import-skycube: a run holds the world lock or another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
[[ -f "${SWC_SKYCUBE_REPORT}" ]] || { echo "no report: status ${status}; see ${SWC_WORK}/logs/import-skycube.log" >&2; exit 1; }
cat "${SWC_SKYCUBE_REPORT}"
exit ${status}
