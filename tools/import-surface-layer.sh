#!/usr/bin/env bash
# Imports the surface layer of regions (the decal materials and the road meshes of tools/convert-materials.sh decals
# export NAME...) into the shared content plugin, with no window (unreal/SoulwardenClient/Scripts/import_surface_layer.py;
# docs/MATERIALS.md). The client layer places them at run time.
# Usage: tools/import-surface-layer.sh NAME... [--max-seconds N] [--wait N]
# It holds SWC_WORK/unreal/import.lock, as tools/import.sh. The commandlet uses at most SWC_CPU_QUOTA (default 400%) and
# SWC_MEMORY_MAX (default 12G).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
source "${SWC_REPO}/tools/lib/world-lock.sh"   # the world lock first, then import.lock (one order for all the tools)
export SWC_SURFACE_REGIONS="" SWC_SURFACE_MAX_SECONDS=""
wait_lock=(-n)
while [[ $# -gt 0 ]]; do
    case "$1" in
        --max-seconds) shift; SWC_SURFACE_MAX_SECONDS="$1" ;;
        --wait) shift; wait_lock=(-w "$1") ;;
        *) SWC_SURFACE_REGIONS+="$1 " ;;
    esac
    shift
done
[[ -n "${SWC_SURFACE_REGIONS}" ]] || { echo "usage: tools/import-surface-layer.sh NAME... [--max-seconds N] [--wait N]" >&2; exit 2; }
"${SWC_REPO}/tools/content-link.sh"
mkdir -p "${SWC_WORK}/logs"
report="${SWC_CONTENT}/decals/import-report.json"
rm -f "${report}"
status=0
world_write "${wait_lock[@]}" flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/import_surface_layer.py" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/import-surface-layer.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "import-surface-layer: a run or a writer holds ${SWC_WORLD_LOCK}, or another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
if [[ -f "${report}" ]]; then
    cat "${report}"
    echo
    grep -h -E "LogPython: Error|Interchange.*Error" "${SWC_WORK}/logs/import-surface-layer.log" | sort | uniq -c | sort -rn | head -5 || true
    python3 -B -c "import json,sys; sys.exit(1 if json.load(open('${report}'))['failed'] else 0)"
else
    echo "no report: the commandlet stopped with status ${status}; see ${SWC_WORK}/logs/import-surface-layer.log" >&2
    exit 1
fi
