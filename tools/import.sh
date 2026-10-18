#!/usr/bin/env bash
# Imports the converted content into the shared content plugin /SwcGameContent with no window (only new or changed
# assets; see unreal/SoulwardenClient/Scripts/import_content.py).
# Usage: tools/import.sh [FILTER] [--force] [--regions NAME...] [--max-seconds N] [--wait N]
#   FILTER         a text that the asset key must contain
#   --regions      only the meshes of these regions (tools/convert.sh world NAME...) and their materials and textures
#   --max-seconds  stop after the batch that passes N seconds (the report has "incomplete": true; call again)
#   --wait         wait at most N seconds for the lock (default: fail at once when another import holds it)
# The editor commandlet uses at most SWC_CPU_QUOTA (default 400%: 4 cores) and SWC_MEMORY_MAX (default 12G).
# One import at a time: the import holds SWC_WORK/unreal/import.lock (all checkouts share the plugin). A long import
# releases the lock at least every 10 minutes (tools/import-world.sh).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
export SWC_IMPORT_FILTER="" SWC_IMPORT_REGIONS="" SWC_IMPORT_MAX_SECONDS=""
wait_lock=(-n)
while [[ $# -gt 0 ]]; do
    case "$1" in
        --force) export SWC_IMPORT_FORCE=1 ;;
        --max-seconds) shift; SWC_IMPORT_MAX_SECONDS="$1" ;;
        --wait) shift; wait_lock=(-w "$1") ;;
        --regions) shift; while [[ $# -gt 0 && "$1" != --* ]]; do SWC_IMPORT_REGIONS+="$1 "; shift; done; continue ;;
        *) SWC_IMPORT_FILTER="$1" ;;
    esac
    shift
done
"${SWC_REPO}/tools/content-link.sh"
mkdir -p "${SWC_WORK}/logs"
rm -f "${SWC_CONTENT}/import-report.json"
status=0
# flock -o: the editor does not get the descriptor of the lock (its trace and cache servers live after it).
flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/import_content.py" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/import.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "import: another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
# The commandlet ends with status 1 when the log has an error line: the report is the result.
if [[ -f "${SWC_CONTENT}/import-report.json" ]]; then
    cat "${SWC_CONTENT}/import-report.json"
    echo
    grep -h -E "LogEditorAssetSubsystem: Error|Interchange.*Error|LogPython: Error" "${SWC_WORK}/logs/import.log" | sort | uniq -c | sort -rn | head -5 || true
    python3 -B -c "import json,sys; sys.exit(1 if json.load(open('${SWC_CONTENT}/import-report.json'))['failed'] else 0)"
else
    echo "no import report: the commandlet stopped with status ${status}; see ${SWC_WORK}/logs/import.log" >&2
    exit 1
fi
