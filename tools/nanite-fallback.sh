#!/usr/bin/env bash
# The full Nanite fallback mesh of the meshes of the minority pairs of SWC_CONTENT/materials/no-nanite.json (AST-288 W2): their cell
# entries with a translucent override have no Nanite (flag bit 2) and draw the fallback mesh, which the default builder reduces
# (unreal/SoulwardenClient/Scripts/nanite_fallback.py). With no window. Usage: tools/nanite-fallback.sh [--wait N]
# It holds the world lock (exclusive) and SWC_WORK/unreal/import.lock, as tools/fix-usage-flags.sh.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
source "${SWC_REPO}/tools/lib/world-lock.sh"
wait_lock=(-n)
[[ "${1:-}" == "--wait" ]] && wait_lock=(-w "$2")
"${SWC_REPO}/tools/content-link.sh"
mkdir -p "${SWC_WORK}/logs"
export SWC_FALLBACK_REPORT="${SWC_WORK}/logs/nanite-fallback-report.json"
rm -f "${SWC_FALLBACK_REPORT}"
status=0
world_write "${wait_lock[@]}" flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/nanite_fallback.py" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/nanite-fallback.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "nanite-fallback: a run or a writer holds ${SWC_WORLD_LOCK}, or another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
if [[ ! -f "${SWC_FALLBACK_REPORT}" ]]; then
    echo "no report: the commandlet stopped with status ${status}; see ${SWC_WORK}/logs/nanite-fallback.log" >&2
    exit 1
fi
grep -h "nanite fallback:" "${SWC_WORK}/logs/nanite-fallback.log" | tail -1 || true
python3 -B -c "import json,sys; r=json.load(open(sys.argv[1])); [print('failed:', f) for f in r['failed']]; sys.exit(1 if r['failed'] else 0)" "${SWC_FALLBACK_REPORT}"
