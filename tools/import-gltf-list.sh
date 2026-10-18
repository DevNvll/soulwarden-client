#!/usr/bin/env bash
# Imports a list of static mesh glTF files into their folders (unreal/SoulwardenClient/Scripts/import_gltf_list.py), with
# the import lock: the cost measure of the M3 rebuild in /SwcGameContent/Test/M3 (docs/MATERIALS.md).
# Usage: tools/import-gltf-list.sh LIST.json REPORT.json [--max-seconds N] [--wait N]
# The commandlet uses at most SWC_CPU_QUOTA (default 400%) and SWC_MEMORY_MAX (default 12G).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
export SWC_GLTF_LIST="$(realpath "${1:?usage: tools/import-gltf-list.sh LIST.json REPORT.json [--max-seconds N] [--wait N]}")"
export SWC_GLTF_REPORT="$(realpath -m "${2:?REPORT.json}")"
shift 2
export SWC_GLTF_MAX_SECONDS=""
wait_lock=(-n)
while [[ $# -gt 0 ]]; do
    case "$1" in
        --max-seconds) shift; SWC_GLTF_MAX_SECONDS="$1" ;;
        --wait) shift; wait_lock=(-w "$1") ;;
    esac
    shift
done
"${SWC_REPO}/tools/content-link.sh"
mkdir -p "${SWC_WORK}/logs"
rm -f "${SWC_GLTF_REPORT}"
status=0
flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/import_gltf_list.py" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/import-gltf-list.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "import-gltf-list: another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
[[ -f "${SWC_GLTF_REPORT}" ]] || { echo "no report: status ${status}; see ${SWC_WORK}/logs/import-gltf-list.log" >&2; exit 1; }
python3 -B -c "import json; d=json.load(open('${SWC_GLTF_REPORT}')); d.pop('batches'); print(json.dumps(d))"
