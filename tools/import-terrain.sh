#!/usr/bin/env bash
# Imports the terrain material of the game into the shared plugin (world fidelity T1; Scripts/import_terrain.py): the
# slices and the three texture arrays, the master material M_SwcTerrain, and the material instance of each region.
# Each call holds the world lock (exclusive: the assets can be in the level) and import.lock, works at most about
# 400 s (SWC_TERRAIN_MAX_SECONDS), and the script calls again until the step is complete; 15 s between two calls.
# Usage: tools/import-terrain.sh [--slices] [--arrays] [--material] [--regions NAME...] [--menu]   (no option: all, with
#        the regions that have a terrain plan with slots; --menu: M_SwcTerrainMesh and the instance of the menu ground,
#        after tools/convert-frontend.sh scene)
# Before: tools/convert.sh terrain-plan --all (the plans and the array slices).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
source "${SWC_REPO}/tools/lib/world-lock.sh"
"${SWC_REPO}/tools/content-link.sh"
steps=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --slices) steps+=(slices) ;;
        --arrays) steps+=(arrays) ;;
        --material) steps+=(material) ;;
        --menu) steps+=(menu) ;;
        --regions) shift; names=(); while [[ $# -gt 0 && "$1" != --* ]]; do names+=("$1"); shift; done
                   steps+=("regions:$(IFS=,; echo "${names[*]}")"); continue ;;
        *) echo "usage: tools/import-terrain.sh [--slices] [--arrays] [--material] [--regions NAME...] [--menu]" >&2; exit 1 ;;
    esac
    shift
done
if [[ ${#steps[@]} -eq 0 ]]; then
    steps=(slices arrays material)
    for plan in "${SWC_CONTENT}"/regions/*/terrain-plan.json; do
        python3 -B -c "import json,sys; sys.exit(0 if json.load(open(sys.argv[1]))['slots'] else 1)" "${plan}" && \
            steps+=("region:$(basename "$(dirname "${plan}")")")
    done
fi
mkdir -p "${SWC_WORK}/logs"
editor_log="${SWC_REPO}/unreal/SoulwardenClient/Saved/Logs/SoulwardenClient.log"
for step in "${steps[@]}"; do
    for attempt in $(seq 1 50); do
        status=0
        SWC_TERRAIN_STEP="${step}" world_write -w 3600 flock -w 3600 -o -E 75 "${SWC_WORK}/unreal/import.lock" \
            systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-16G}" \
            nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
            -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/import_terrain.py" "${SWC_EDITOR_DDC}" \
            -nullrhi -unattended -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/import-terrain.log" 2>&1 || status=$?
        report=$(grep -ho 'SWC terrain REPORT .*' "${editor_log}" | tail -1 || true)   # none: the failure below
        echo "$(date -u +%H:%M:%S) ${step} call ${attempt}: status ${status}; ${report:-no report}"
        if [[ -z "${report}" ]]; then
            cp "${editor_log}" "${SWC_WORK}/logs/import-terrain-failed.log" 2>/dev/null || true
            grep -h "LogPython: Error\|Error:" "${editor_log}" | head -5 >&2 || true
            exit 1
        fi
        sleep 15   # the runs and the other writers get the lock between two calls (a waiting writer goes first)
        [[ "${report}" == *'"incomplete": true'* ]] || break
    done
done
