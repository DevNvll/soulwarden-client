#!/usr/bin/env bash
# Cooks and stages the client for Linux (docs/WORLD.md "The cooked build"): the game target, then UAT BuildCookRun with the
# default map (the whole world, NewWorld) and the content that the client loads by path (DirectoriesToAlwaysCook of
# DefaultGame.ini), the Lua files staged, IoStore. The staged build goes to SWC_COOKED (default SWC_WORK/cooked); its stamp
# SWC_COOKED/Linux/swc-cook.json has the commit, the time, and the map (tools/play.sh and tools/dev-run.sh --cooked read it).
# Usage: tools/cook.sh [--map /Game/PATH/MAP] [--skip-build] [--skip-cook] [--wait N]
#   --map         a test level instead of the default map: a temporary Config/Linux/LinuxEngine.ini sets its GameDefaultMap
#                 (the cooker reads the default map from the config files of the target platform, not from the command line),
#                 and it goes away after the cook (the staged build then starts that map)
#   --skip-build  no build of the game target (its binary of an earlier build)
#   --skip-cook   only the stage of the files of an earlier cook (for example after a change of the Lua files)
#   --wait        wait at most N seconds for import.lock (default 3600)
# The rules: the RAM rule (14 GB available before the start), one Unreal process; the cook holds import.lock (it reads the shared
# plugin), runs in a memory scope (SWC_COOK_MEMORY_MAX, default 20G; the cooker collects garbage at SWC_COOK_MEMORY_GC MiB,
# default 14000) with 12 cores at nice 10 (SWC_COOK_CPU_QUOTA, default 1200%: the shader compile of a cold cook was the limit with
# 4 cores), and stops when the disk of SWC_WORK goes above 85 %. The log: SWC_WORK/logs/cook.log. An incremental cook
# (the second one) cooks again only the packages that changed (UAT -iterate).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
map="" build=1 cook=1 wait=3600
while [[ $# -gt 0 ]]; do
    case "$1" in
        --map) shift; map="$1" ;;
        --skip-build) build=0 ;;
        --skip-cook) cook=0 ;;
        --wait) shift; wait="$1" ;;
        *) echo "usage: tools/cook.sh [--map /Game/PATH/MAP] [--skip-build] [--skip-cook] [--wait N]" >&2; exit 1 ;;
    esac
    shift
done
cooked="${SWC_COOKED:-${SWC_WORK}/cooked}"
platform_ini="${SWC_REPO}/unreal/SoulwardenClient/Config/Linux/LinuxEngine.ini"
log="${SWC_WORK}/logs/cook.log"
mkdir -p "${SWC_WORK}/logs" "${cooked}"
disk() { df --output=pcent "${SWC_WORK}" | tail -1 | tr -dc 0-9; }
made_ini=0 guard=""
cleanup() {
    [[ -n "${guard}" ]] && kill "${guard}" 2>/dev/null
    if [[ ${made_ini} -eq 1 ]]; then
        rm -f "${platform_ini}"
        rmdir "$(dirname "${platform_ini}")" 2>/dev/null || true
    fi
}
trap cleanup EXIT
until [ "$(free -g | awk '/Mem:/{print $7}')" -ge 14 ]; do
    echo "cook: the RAM has less than 14 GB available: waiting" >&2
    sleep 60
done
if [[ -n "${map}" ]]; then
    [[ -e "${platform_ini}" ]] && { echo "cook: ${platform_ini} exists; --map needs it free" >&2; exit 1; }
    mkdir -p "$(dirname "${platform_ini}")"
    printf '; tools/cook.sh --map (temporary)\n[/Script/EngineSettings.GameMapsSettings]\nGameDefaultMap=%s.%s\n' "${map}" "${map##*/}" > "${platform_ini}"
    made_ini=1
fi
if [[ ${build} -eq 1 ]]; then
    flock -w "${wait}" "${SWC_WORK}/unreal/import.lock" "${SWC_REPO}/tools/build.sh" game > "${SWC_WORK}/logs/cook-build.log" 2>&1 \
        || { echo "cook: the game target did not build; see ${SWC_WORK}/logs/cook-build.log" >&2; exit 1; }
fi
steps=(-stage -pak -iostore -stagingdirectory="${cooked}")
[[ ${cook} -eq 1 ]] && steps=(-cook -iterate "${steps[@]}") || steps=(-skipcook "${steps[@]}")
[[ -n "${map}" ]] && steps+=(-map="${map}")
# The disk guard: the cook stops above 85 %.
(
    while sleep 30; do
        if (( $(disk) > 85 )); then
            echo "$(date -u +%H:%M:%S) cook: the disk is above 85 %: the cook stops" >> "${log}"
            pkill -INT -f "UnrealEditor-Cmd.*-run=Cook" || true
            exit 0
        fi
    done
) &
guard=$!
start=${SECONDS}
status=0
echo "$(date -u +%H:%M:%S) cook: start (map ${map:-the default}, disk $(disk) %)" | tee -a "${log}.summary"
flock -w "${wait}" "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_COOK_CPU_QUOTA:-1200%}" -p MemoryMax="${SWC_COOK_MEMORY_MAX:-20G}" \
    nice -n 10 "${UE_ROOT}/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="${SWC_UPROJECT}" -noP4 -platform=Linux \
    -clientconfig=Development -skipbuild -nocompileeditor "${steps[@]}" -unattended -utf8output \
    -AdditionalCookerOptions="-ini:Editor:[CookSettings]:MemoryMaxUsedPhysical=${SWC_COOK_MEMORY_GC:-14000}" \
    > "${log}" 2>&1 || status=$?
echo "$(date -u +%H:%M:%S) cook: status ${status}, $((SECONDS - start)) s, disk $(disk) %, staged $(du -s --block-size=1M "${cooked}" | cut -f1) MiB" \
    | tee -a "${log}.summary"
[[ ${status} -eq 0 ]] || { echo "cook: see ${log}" >&2; exit 1; }
source "${SWC_REPO}/tools/lib/cooked.sh"
python3 -B -c "import json, sys, time; json.dump({'commit': sys.argv[1], 'code': sys.argv[2], 'time': time.time(), 'map': sys.argv[3] or 'default'}, open(sys.argv[4], 'w'))" \
    "$(git -C "${SWC_REPO}" rev-parse HEAD)" "$(cooked_code_hash)" "${map}" "${cooked}/Linux/swc-cook.json"
grep -hE "Total Shaders|Unique Shadermaps|Packages Cooked" "${log}" | tail -3 || true
