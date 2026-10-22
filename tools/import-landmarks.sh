#!/usr/bin/env bash
# Imports the landmarks of the world (AST-209, docs/WORLD.md "The landmarks"): the sky maelstrom of SWC_CONTENT/landmarks/maelstrom.json
# (python3 -B -m swc_convert.landmarks write): its meshes, textures, two parents, and instances, all new in
# /SwcGameContent/Landmarks/Maelstrom (unreal/SoulwardenClient/Scripts/import_landmarks.py), with the world lock and the import lock.
# Usage: tools/import-landmarks.sh [--force] [--wait N]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
wait_lock=(-n)
export SWC_LANDMARKS="${SWC_CONTENT}/landmarks/maelstrom.json" SWC_LANDMARKS_FORCE=0 SWC_LANDMARKS_REPORT="${SWC_WORK}/logs/import-landmarks.json"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --force) SWC_LANDMARKS_FORCE=1 ;;
        --wait) shift; wait_lock=(-w "$1") ;;
        *) echo "usage: tools/import-landmarks.sh [--force] [--wait N]" >&2; exit 2 ;;
    esac
    shift
done
[[ -f "${SWC_LANDMARKS}" ]] || { echo "no ${SWC_LANDMARKS}: run python3 -B -m swc_convert.landmarks write" >&2; exit 1; }
"${SWC_REPO}/tools/content-link.sh"
mkdir -p "${SWC_WORK}/logs"
rm -f "${SWC_LANDMARKS_REPORT}"
status=0
# The world lock first (exclusive: the runs load these assets; tools/lib/world-lock.sh, docs/WORLD.md), then import.lock. flock -o:
# the editor does not get the descriptor of the lock (its trace and cache servers live after it).
source "${SWC_REPO}/tools/lib/world-lock.sh"
world_write "${wait_lock[@]}" flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/import_landmarks.py" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/import-landmarks.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "import-landmarks: a run holds the world lock or another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
[[ -f "${SWC_LANDMARKS_REPORT}" ]] || { echo "no report: status ${status}; see ${SWC_WORK}/logs/import-landmarks.log" >&2; exit 1; }
cat "${SWC_LANDMARKS_REPORT}"
exit ${status}
