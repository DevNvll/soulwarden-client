#!/usr/bin/env bash
# Imports a list of static mesh glTF files into their folders (unreal/SoulwardenClient/Scripts/import_gltf_list.py), with
# the import lock: the cost measure of the M3 rebuild in /SwcGameContent/Test/M3, and the M3 rebuild into the existing
# mesh folders (content/materials/rebuild-import.json; docs/MATERIALS.md).
# Usage: tools/import-gltf-list.sh LIST.json REPORT.json [--max-seconds N] [--wait N] [--force]
# The commandlet uses at most SWC_CPU_QUOTA (default 400%) and SWC_MEMORY_MAX (default 12G).
# A call holds the world lock exclusively first (tools/lib/world-lock.sh: it rewrites meshes that the runs stream; one
# order for all the tools), then import.lock.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
source "${SWC_REPO}/tools/lib/world-lock.sh"
export SWC_GLTF_LIST="$(realpath "${1:?usage: tools/import-gltf-list.sh LIST.json REPORT.json [--max-seconds N] [--wait N]}")"
export SWC_GLTF_REPORT="$(realpath -m "${2:?REPORT.json}")"
shift 2
export SWC_GLTF_MAX_SECONDS="" SWC_GLTF_FORCE=""
wait_lock=(-n)
while [[ $# -gt 0 ]]; do
    case "$1" in
        --max-seconds) shift; SWC_GLTF_MAX_SECONDS="$1" ;;
        --wait) shift; wait_lock=(-w "$1") ;;
        --force) SWC_GLTF_FORCE=1 ;;   # import each item again (a measure; a next call starts again: use it once)
    esac
    shift
done
"${SWC_REPO}/tools/content-link.sh"
mkdir -p "${SWC_WORK}/logs"
rm -f "${SWC_GLTF_REPORT}"
status=0
world_write "${wait_lock[@]}" flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/import_gltf_list.py" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/import-gltf-list.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "import-gltf-list: a run or a writer holds ${SWC_WORLD_LOCK}, or another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
[[ -f "${SWC_GLTF_REPORT}" ]] || { echo "no report: status ${status}; see ${SWC_WORK}/logs/import-gltf-list.log" >&2; exit 1; }
python3 -B -c "import json; d=json.load(open('${SWC_GLTF_REPORT}')); d.pop('batches'); print(json.dumps(d))"
