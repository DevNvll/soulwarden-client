#!/usr/bin/env bash
# Cooks and stages the client for Linux (docs/WORLD.md "The cooked build"): the game target, then UAT BuildCookRun with the
# default map (the whole world, NewWorld) and the content that the client loads by path (DirectoriesToAlwaysCook of
# DefaultGame.ini), the Lua files staged, IoStore. The staged build goes to SWC_COOKED (default SWC_WORK/cooked); its stamp
# SWC_COOKED/Linux/swc-cook.json has the commit, the time, and the map (tools/play.sh and tools/dev-run.sh --cooked read it).
# Usage: tools/cook.sh [--map /Game/PATH/MAP] [--skip-build] [--skip-cook] [--wait N] [--full]
#   --map         a test level instead of the default map: a temporary Config/Linux/LinuxEngine.ini sets its GameDefaultMap
#                 (the cooker reads the default map from the config files of the target platform, not from the command line),
#                 and it goes away after the cook (the staged build then starts that map)
#   --skip-build  no build of the game target (its binary of an earlier build)
#   --skip-cook   only the stage of the files of an earlier cook (for example after a change of the Lua files)
#   --wait        wait at most N seconds for import.lock (default 3600)
#   --full        allow a full cook. The legacy incremental cook (no Zen store) deletes all cooked packages when the ini settings
#                 change (the config files, and the cooker options below: keep them constant). Before such a cook (the config
#                 hash differs from the stamp of the last good cook, or the cooked folder of the checkout is almost empty), this
#                 tool stops with status 3 and says "FULL cook" unless --full is given. A full cook of the world needs a quiet
#                 computer (docs/WORLD.md "The cooked build"). A run-time setting goes into the code, not into an ini file.
# The rules: the RAM rule (14 GB available before the start), one Unreal process; the cook holds import.lock (it reads the shared
# plugin), runs in a memory scope (SWC_COOK_MEMORY_MAX, default 22G, with at most 6G of swap: the cooker must not thrash in swap)
# with 12 cores at nice 10 (SWC_COOK_CPU_QUOTA, default 1200%: the shader compile of a cold cook was the limit with 4 cores), and
# stops when the free space of the disk of SWC_WORK goes below SWC_COOK_MIN_FREE_GB (default 100 GB; AST-273: one cook and one staged
# build put the 1.9 TB disk at about 86 %, so a guard of 85 % stopped each cook that followed). The cooker options are constants (COOKER_OPTIONS: garbage collection at
# 20,000 MiB used or 8,000 MiB available, see below; 8 shader workers): a change makes the next cook full. The log: SWC_WORK/logs/cook.log. An incremental cook
# (the second one) cooks again only the packages that changed (UAT -iterate).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
map="" build=1 cook=1 wait=3600 full=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --map) shift; map="$1" ;;
        --skip-build) build=0 ;;
        --skip-cook) cook=0 ;;
        --wait) shift; wait="$1" ;;
        --full) full=1 ;;
        *) echo "usage: tools/cook.sh [--map /Game/PATH/MAP] [--skip-build] [--skip-cook] [--wait N] [--full]" >&2; exit 1 ;;
    esac
    shift
done
cooked="${SWC_COOKED:-${SWC_WORK}/cooked}"
platform_ini="${SWC_REPO}/unreal/SoulwardenClient/Config/Linux/LinuxEngine.ini"
log="${SWC_WORK}/logs/cook.log"
# The options of the cooker: constants (they are in the ini settings of the legacy incremental cook; 2026-10-22, AST-229).
# The memory (AST-273, 2026-10-23): the computer has 46 GB of RAM, and the desktop programs use about 18 GB. The full cook of
# 175,045 packages with a 30G scope was stopped by the guard at "available" 1.1 GB after 433 s (64,732 packages; 33 GB of virtual
# memory). The scope is now 22G (22 + 18 GB, plus about 2.5 GB of RAM for the zram of the 6G of swap of the scope, leaves about
# 3.5 GB). MemoryMinFreePhysical 8000: the cooker collects garbage when the whole computer has less than 8 GB available (the
# engine compares it with the available physical memory, CookGarbageCollect.cpp), before the guard stops at 3 GB; this follows the
# desktop programs. MemoryMaxUsedPhysical 20000: under the scope (14000 gave the GC stall and the assertion of 2026-10-22).
COOKER_OPTIONS="-ini:Editor:[CookSettings]:MemoryMaxUsedPhysical=20000 -ini:Editor:[CookSettings]:MemoryMinFreePhysical=8000 -ini:Engine:[DevOptions.Shaders]:ShaderCompilerCoreCountThreshold=64 -ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=8"
# config_hash: the project config files (Default*.ini; not the temporary Config/Linux of --map) and the cooker options.
config_hash() {
    { (cd "${SWC_REPO}/unreal/SoulwardenClient/Config" && sha1sum Default*.ini); echo "${COOKER_OPTIONS}"; } | sha1sum | cut -c1-16
}
mkdir -p "${SWC_WORK}/logs" "${cooked}"
disk() { df --output=pcent "${SWC_WORK}" | tail -1 | tr -dc 0-9; }
free_gb() { df --output=avail -B1G "${SWC_WORK}" | tail -1 | tr -dc 0-9; }
min_free_gb="${SWC_COOK_MIN_FREE_GB:-100}"
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
# The full cook check (before the build: a stop costs nothing).
if [[ ${cook} -eq 1 && -z "${map}" ]]; then
    stamp="${cooked}/Linux/swc-cook.json"
    last=$(python3 -B -c "import json, sys; print(json.load(open(sys.argv[1])).get('config', ''))" "${stamp}" 2>/dev/null || true)
    files=$( (find "${SWC_REPO}/unreal/SoulwardenClient/Saved/Cooked/Linux" -name "*.uasset" 2>/dev/null || true) | head -20000 | wc -l)
    why=""
    [[ "${last}" != "$(config_hash)" ]] && why="the config (Config/Default*.ini or the cooker options) changed since the last good cook (stamp: ${last:-none})"
    (( files < 20000 )) && why="${why:+${why}; }the cooked folder of the checkout has only ${files} packages"
    if [[ -n "${why}" && ${full} -eq 0 ]]; then
        echo "cook: FULL cook: ${why}. A full cook of the world needs a quiet computer (docs/WORLD.md \"The cooked build\"); give --full" >&2
        exit 3
    fi
fi
if [[ ${build} -eq 1 ]]; then
    flock -w "${wait}" "${SWC_WORK}/unreal/import.lock" "${SWC_REPO}/tools/build.sh" game > "${SWC_WORK}/logs/cook-build.log" 2>&1 \
        || { echo "cook: the game target did not build; see ${SWC_WORK}/logs/cook-build.log" >&2; exit 1; }
fi
steps=(-stage -pak -iostore -stagingdirectory="${cooked}")
[[ ${cook} -eq 1 ]] && steps=(-cook -iterate "${steps[@]}") || steps=(-skipcook "${steps[@]}")
[[ -n "${map}" ]] && steps+=(-map="${map}")
# The disk guard: the cook stops when the free space goes below SWC_COOK_MIN_FREE_GB.
if (( $(free_gb) < min_free_gb )); then
    echo "cook: the disk of ${SWC_WORK} has $(free_gb) GB free, less than ${min_free_gb} GB (SWC_COOK_MIN_FREE_GB): no cook" >&2
    exit 3
fi
(
    while sleep 30; do
        if (( $(free_gb) < min_free_gb )); then
            echo "$(date -u +%H:%M:%S) cook: the disk has less than ${min_free_gb} GB free: the cook stops" >> "${log}"
            pkill -INT -f "UnrealEditor-Cmd.*-run=Cook" || true
            exit 0
        fi
    done
) &
guard=$!
start=${SECONDS}
status=0
echo "$(date -u +%H:%M:%S) cook: start (map ${map:-the default}, disk $(disk) %, $(free_gb) GB free)" | tee -a "${log}.summary"
flock -w "${wait}" "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_COOK_CPU_QUOTA:-1200%}" -p MemoryMax="${SWC_COOK_MEMORY_MAX:-22G}" -p MemorySwapMax=6G \
    nice -n 10 "${UE_ROOT}/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="${SWC_UPROJECT}" -noP4 -platform=Linux \
    -clientconfig=Development -skipbuild -nocompileeditor "${steps[@]}" -unattended -utf8output \
    -AdditionalCookerOptions="${COOKER_OPTIONS}" \
    > "${log}" 2>&1 || status=$?
echo "$(date -u +%H:%M:%S) cook: status ${status}, $((SECONDS - start)) s, disk $(disk) %, staged $(du -s --block-size=1M "${cooked}" | cut -f1) MiB" \
    | tee -a "${log}.summary"
[[ ${status} -eq 0 ]] || { echo "cook: see ${log}" >&2; exit 1; }
source "${SWC_REPO}/tools/lib/cooked.sh"
python3 -B -c "import json, sys, time; json.dump({'commit': sys.argv[1], 'code': sys.argv[2], 'time': time.time(), 'map': sys.argv[3] or 'default', 'config': sys.argv[5]}, open(sys.argv[4], 'w'))" \
    "$(git -C "${SWC_REPO}" rev-parse HEAD)" "$(cooked_code_hash)" "${map}" "${cooked}/Linux/swc-cook.json" "$(config_hash)"
grep -hE "Total Shaders|Unique Shadermaps|Packages Cooked" "${log}" | tail -3 || true
