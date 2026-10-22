#!/usr/bin/env bash
# The falling snow (AST-257, docs/ENVIRONMENT.md): the linear flake atlas and the flake material M_SwcSnowFlake
# (unreal/SoulwardenClient/Scripts/import_snow.py; new assets only). The import lock, no window.
# Usage: tools/import-snow.sh [--force] [--masters] [--wait N]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
wait_lock=(-n)
export SWC_SNOW_PNG="${SWC_CONTENT}/environment/sky/snowflakes.png" SWC_SNOW_DIR="${SWC_CONTENT}/environment/snow" SWC_SNOW_FORCE=0 SWC_SNOW_MASTERS=0 SWC_SNOW_REPORT="${SWC_WORK}/logs/import-snow.json"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --force) SWC_SNOW_FORCE=1 ;;
        --masters) SWC_SNOW_MASTERS=1 ;;   # also the surface masters with the snow call (the terrain masters: tools/import-terrain.sh)
        --wait) shift; wait_lock=(-w "$1") ;;
        *) echo "usage: tools/import-snow.sh [--force] [--masters] [--wait N]" >&2; exit 2 ;;
    esac
    shift
done
[[ -f "${SWC_SNOW_PNG}" ]] || { echo "no ${SWC_SNOW_PNG}: run tools/convert-environment.sh sky" >&2; exit 1; }
"${SWC_REPO}/tools/content-link.sh"
mkdir -p "${SWC_WORK}/logs"
rm -f "${SWC_SNOW_REPORT}"
status=0
source "${SWC_REPO}/tools/lib/world-lock.sh"
world_write "${wait_lock[@]}" flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/import_snow.py" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/import-snow.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "import-snow: a run holds the world lock or another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
[[ -f "${SWC_SNOW_REPORT}" ]] || { echo "no report: status ${status}; see ${SWC_WORK}/logs/import-snow.log" >&2; exit 1; }
cat "${SWC_SNOW_REPORT}"
exit ${status}
