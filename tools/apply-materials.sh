#!/usr/bin/env bash
# Applies the material specs of the converter (tools/convert-materials.sh instances) to the material instances of the
# shared content plugin, with no window (unreal/SoulwardenClient/Scripts/apply_materials.py; docs/MATERIALS.md).
# Usage: tools/apply-materials.sh [FILTER] [--keys FILE] [--prefetch] [--max-seconds N] [--wait N]
#   FILTER         a text that the material key must contain
#   --keys         only the material keys of a JSON list
#   --prefetch     only import the textures of the candidate specs (instances-candidate.json), no instance change
#   --max-seconds  stop after the item that passes N seconds (the report has "incomplete": true; call again)
#   --wait         wait at most N seconds for the import lock (default: fail at once when another import holds it)
# The editor commandlet uses at most SWC_CPU_QUOTA (default 400%: 4 cores) and SWC_MEMORY_MAX (default 12G).
# It holds SWC_WORK/unreal/import.lock, as tools/import.sh (one writer of the plugin at a time).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
export SWC_MATERIALS_FILTER="" SWC_MATERIALS_KEYS="" SWC_MATERIALS_MAX_SECONDS="" SWC_MATERIALS_PREFETCH=""
wait_lock=(-n)
while [[ $# -gt 0 ]]; do
    case "$1" in
        --keys) shift; SWC_MATERIALS_KEYS="$(realpath "$1")" ;;
        --prefetch) SWC_MATERIALS_PREFETCH=1 ;;
        --max-seconds) shift; SWC_MATERIALS_MAX_SECONDS="$1" ;;
        --wait) shift; wait_lock=(-w "$1") ;;
        *) SWC_MATERIALS_FILTER="$1" ;;
    esac
    shift
done
"${SWC_REPO}/tools/content-link.sh"
mkdir -p "${SWC_WORK}/logs"
report="${SWC_CONTENT}/materials/apply-report.json"
rm -f "${report}"
status=0
flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/apply_materials.py" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/apply-materials.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "apply-materials: another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
if [[ -f "${report}" ]]; then
    cat "${report}"
    echo
    grep -h -E "LogPython: Error|LogMaterial.*Error|Shader.*error" "${SWC_WORK}/logs/apply-materials.log" | sort | uniq -c | sort -rn | head -5 || true
    # A parent that does not compile: Unreal draws its default material (the log has a warning, not an error).
    if grep -h "Failed to compile Material" "${SWC_WORK}/logs/apply-materials.log" | grep -o "/SwcGameContent/[A-Za-z0-9_/.]*" | sort -u | grep .; then
        echo "apply-materials: a material does not compile" >&2
        exit 1
    fi
    python3 -B -c "import json,sys; sys.exit(1 if json.load(open('${report}'))['failed'] else 0)"
else
    echo "no report: the commandlet stopped with status ${status}; see ${SWC_WORK}/logs/apply-materials.log" >&2
    exit 1
fi
