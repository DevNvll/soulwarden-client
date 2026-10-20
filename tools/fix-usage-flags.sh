#!/usr/bin/env bash
# Sets the material usage flags that the client logs as missing ("Material PATH missing usage flag NAME! Default Material will
# be used in game."; the default material of the engine is then drawn) and saves the materials, with no window
# (unreal/SoulwardenClient/Scripts/usage_flags.py; docs/WORLD.md "The loading"). Only the usage flags change.
# Usage: tools/fix-usage-flags.sh [--wait N] [--list] [CLIENT_LOG...]
#   CLIENT_LOG  the client logs to read (default: the 300 newest client logs of SWC_WORK/logs/dev-run)
#   --list      only print the materials and the flags, change nothing
#   --wait      wait at most N seconds for the world lock and the import lock (default: fail at once when they are held)
# It holds the world lock (exclusive: a run can stream these materials) and SWC_WORK/unreal/import.lock, as
# tools/apply-materials.sh.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
source "${SWC_REPO}/tools/lib/world-lock.sh"   # the world lock first, then import.lock (one order for all the tools)
wait_lock=(-n)
list_only=0
logs=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --wait) shift; wait_lock=(-w "$1") ;;
        --list) list_only=1 ;;
        *) logs+=("$(realpath "$1")") ;;
    esac
    shift
done
if [[ ${#logs[@]} -eq 0 ]]; then
    mapfile -t logs < <(cd "${SWC_WORK}/logs/dev-run" && /bin/ls -t | grep -- '-client\.log$' | head -300 | sed "s|^|${SWC_WORK}/logs/dev-run/|")
fi
list="${SWC_WORK}/unreal/usage-flags.txt"
printf '%s\0' "${logs[@]}" | xargs -0 grep -h -e 'missing usage flag' -- 2>/dev/null \
    | sed -E 's/.*Material ([^ ]+) missing usage flag ([^!]+)!.*/\1 \2/' | sort -u > "${list}" || true
echo "fix-usage-flags: $(wc -l < "${list}") material flags in ${#logs[@]} client logs"
if [[ ${list_only} -eq 1 || ! -s "${list}" ]]; then
    cat "${list}"
    exit 0
fi
"${SWC_REPO}/tools/content-link.sh"
mkdir -p "${SWC_WORK}/logs"
export SWC_USAGE_LIST="${list}" SWC_USAGE_REPORT="${SWC_WORK}/logs/usage-flags-report.json"
rm -f "${SWC_USAGE_REPORT}"
status=0
world_write "${wait_lock[@]}" flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/usage_flags.py" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/usage-flags.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "fix-usage-flags: a run or a writer holds ${SWC_WORLD_LOCK}, or another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
if [[ ! -f "${SWC_USAGE_REPORT}" ]]; then
    echo "no report: the commandlet stopped with status ${status}; see ${SWC_WORK}/logs/usage-flags.log" >&2
    exit 1
fi
grep -h "usage flags:" "${SWC_WORK}/logs/usage-flags.log" | tail -1
python3 -B -c "import json,sys; r=json.load(open(sys.argv[1])); [print('failed:', f) for f in r['failed']]; sys.exit(1 if r['failed'] else 0)" "${SWC_USAGE_REPORT}"
