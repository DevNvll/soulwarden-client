#!/usr/bin/env bash
# Imports the converted content into the Unreal project with no window. Usage: tools/import.sh [FILTER] [--force]
# The editor commandlet uses at most SWC_CPU_QUOTA (default 400%: 4 cores) and SWC_MEMORY_MAX (default 12G).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
export SWC_IMPORT_FILTER="${1:-}"
[[ "${2:-}" == "--force" ]] && export SWC_IMPORT_FORCE=1
mkdir -p "${SWC_WORK}/logs"
rm -f "${SWC_CONTENT}/import-report.json"
status=0
systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/import_content.py" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/import.log" 2>&1 || status=$?
# The commandlet ends with status 1 when the log has an error line: the report is the result.
if [[ -f "${SWC_CONTENT}/import-report.json" ]]; then
    cat "${SWC_CONTENT}/import-report.json"
    echo
    grep -h -E "LogEditorAssetSubsystem: Error|Interchange.*Error" "${SWC_WORK}/logs/import.log" | sort | uniq -c | sort -rn | head -5 || true
    python3 -B -c "import json,sys; sys.exit(1 if json.load(open('${SWC_CONTENT}/import-report.json'))['failed'] else 0)"
else
    echo "no import report: the commandlet stopped with status ${status}; see ${SWC_WORK}/logs/import.log" >&2
    exit 1
fi
