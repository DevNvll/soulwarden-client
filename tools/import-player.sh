#!/usr/bin/env bash
# Imports the converted player content (tools/convert-player.sh) into the shared content plugin, /SwcGameContent/Player,
# with no window.
# Usage: tools/import-player.sh [--force]
# The editor commandlet uses at most SWC_CPU_QUOTA (default 400%: 4 cores) and SWC_MEMORY_MAX (default 12G).
# One import at a time: the import waits for SWC_WORK/unreal/import.lock (the lock of tools/import.sh; 30 min at most).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
[[ "${1:-}" == "--force" ]] && export SWC_IMPORT_FORCE=1
"${SWC_REPO}/tools/content-link.sh"
mkdir -p "${SWC_WORK}/logs" "${SWC_WORK}/unreal"
report="${SWC_CONTENT}/player/import-report.json"
rm -f "${report}"
status=0
# flock -o: the editor does not get the descriptor of the lock (its trace and cache servers live after it).
flock -w 1800 -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/import_player.py" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/import-player.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "import-player: another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
if [[ -f "${report}" ]]; then
    cat "${report}"
    echo
    grep -h -E "Interchange.*Error|LogPython: Error" "${SWC_WORK}/logs/import-player.log" | sort | uniq -c | sort -rn | head -5 || true
    python3 -B -c "import json,sys; sys.exit(1 if json.load(open('${report}'))['failed'] else 0)"
else
    echo "no import report: the commandlet stopped with status ${status}; see ${SWC_WORK}/logs/import-player.log" >&2
    exit 1
fi
