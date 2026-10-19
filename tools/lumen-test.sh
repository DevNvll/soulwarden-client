#!/usr/bin/env bash
# The Lumen mesh data test (stage E2, docs/ENVIRONMENT.md): the meshes of the inn of Oxboro and of two houses of the spawn lane
# (tools/lib/lumen_test.py) are copied into /SwcGameContent/Test/E2 with mesh distance fields and Lumen cards
# (unreal/SoulwardenClient/Scripts/lumen_test_copies.py, with the import lock). The import settings of the world do not change.
# A client run with -SwcLumenTest uses the copies (SWC_CONTENT/environment/lumen-test.json; Environment/SwcLumenTest.h).
# Usage: tools/lumen-test.sh [--wait N] [--min-size CM]
# The commandlet uses at most SWC_CPU_QUOTA (default 400%) and SWC_MEMORY_MAX (default 12G).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
wait_lock=(-n)
export SWC_LUMEN_MIN_SIZE=100
while [[ $# -gt 0 ]]; do
    case "$1" in
        --wait) shift; wait_lock=(-w "$1") ;;
        --min-size) shift; SWC_LUMEN_MIN_SIZE="$1" ;;
        *) echo "usage: tools/lumen-test.sh [--wait N] [--min-size CM]" >&2; exit 2 ;;
    esac
    shift
done
folder="${SWC_WORK}/lumen-test"
mkdir -p "${folder}" "${SWC_WORK}/logs" "${SWC_CONTENT}/environment"
export SWC_LUMEN_LIST="${folder}/list.json" SWC_LUMEN_REPORT="${folder}/report.json" SWC_LUMEN_MAP="${SWC_CONTENT}/environment/lumen-test.json"
python3 -B "${SWC_REPO}/tools/lib/lumen_test.py" "${SWC_LUMEN_LIST}"
"${SWC_REPO}/tools/content-link.sh"
rm -f "${SWC_LUMEN_REPORT}"
status=0
# The world lock first (exclusive: the runs load these assets; tools/lib/world-lock.sh, docs/WORLD.md), then import.lock. flock -o:
# the editor does not get the descriptor of the lock (its trace and cache servers live after it).
source "${SWC_REPO}/tools/lib/world-lock.sh"
world_write "${wait_lock[@]}" flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/lumen_test_copies.py" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/lumen-test.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "lumen-test: a run holds the world lock or another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
[[ -f "${SWC_LUMEN_REPORT}" ]] || { echo "no report: status ${status}; see ${SWC_WORK}/logs/lumen-test.log" >&2; exit 1; }
python3 -B -c "import json, sys; d = json.load(open(sys.argv[1])); d.pop('meshes'); print(json.dumps(d))" "${SWC_LUMEN_REPORT}"
