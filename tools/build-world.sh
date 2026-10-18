#!/usr/bin/env bash
# Builds the open world level (/SwcGameContent/World/NewWorld) region by region with no window
# (unreal/SoulwardenClient/Scripts/build_world.py). Each call holds import.lock (the level is in the shared plugin)
# and stops after about 4 minutes of work (SWC_WORLD_MAX_SECONDS; a region has three steps); the script calls again
# until the region is in the level, and waits 15 s between two calls, so that an import of another track gets the
# lock. Each call writes one line to the output: the status, the seconds with the lock, and the last region line.
# The editor log of a failed call is kept in SWC_WORK/logs/build-world-REGION-failed.log.
# Usage: tools/build-world.sh NAME... | --playable | --all  [--force: make the level again]
#   [--import: before each region, tools/import-world.sh REGION (its meshes, textures, materials)]
#   [--cells: a region in the level gets new cell actors when its cells version is old (build_world.py CELLS_VERSION)]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
"${SWC_REPO}/tools/content-link.sh"
regions=()
import=0
for argument in "$@"; do
    case "${argument}" in
        --force) export SWC_WORLD_FORCE=1 ;;
        --import) import=1 ;;
        --cells) export SWC_WORLD_CELLS=1 ;;
        --all|--playable)
            mapfile -t -O "${#regions[@]}" regions < <(cd "${SWC_REPO}/converter" && python3 -B -c "
import sys
from swc_convert import census
print('\n'.join(census.world_regions() if sys.argv[1] == 'all' else census.playable_regions()))" "${argument#--}") ;;
        *) regions+=("${argument}") ;;
    esac
done
[[ ${#regions[@]} -gt 0 ]] || { echo "usage: tools/build-world.sh NAME... | --playable | --all [--force]" >&2; exit 1; }
mkdir -p "${SWC_WORK}/logs"
in_level() {   # with --cells: in the level with cell actors of the current rule (CELLS_VERSION 4 of build_world.py)
    [[ -f "${SWC_GAME_CONTENT}/world-state.json" ]] && \
        python3 -B -c "import json,sys; r = json.load(open(sys.argv[1]))['regions'].get(sys.argv[2]); sys.exit(0 if r is not None and (sys.argv[3] != '1' or r.get('cells version', 1) >= 4) else 1)" \
            "${SWC_GAME_CONTENT}/world-state.json" "$1" "${SWC_WORLD_CELLS:-}"
}
editor_log="${SWC_REPO}/unreal/SoulwardenClient/Saved/Logs/SoulwardenClient.log"
for region in "${regions[@]}"; do
    [[ ${import} -eq 1 ]] && "${SWC_REPO}/tools/import-world.sh" "${region}"
    if [[ -z "${SWC_WORLD_FORCE:-}" ]] && in_level "${region}"; then
        echo "$(date -u +%H:%M:%S) ${region}: in the level"
        continue
    fi
    for attempt in $(seq 1 10); do
        status=0
        rm -f "${SWC_WORK}/logs/build-world-seconds"
        SWC_WORLD_REGIONS="${region}" flock -w 3600 -o -E 75 "${SWC_WORK}/unreal/import.lock" \
            bash -c 'out="$1"; shift; start=${SECONDS}; status=0; "$@" || status=$?; echo $((SECONDS - start)) > "${out}"; exit ${status}' _ \
            "${SWC_WORK}/logs/build-world-seconds" \
            systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-16G}" \
            nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
            -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/build_world.py" "${SWC_EDITOR_DDC}" \
            -AllowCommandletRendering -RenderOffscreen -unattended -nosplash -nosound -nopause -stdout \
            > "${SWC_WORK}/logs/build-world.log" 2>&1 || status=$?
        echo "$(date -u +%H:%M:%S) ${region} build ${attempt}: status ${status}, $(cat "${SWC_WORK}/logs/build-world-seconds" 2>/dev/null || echo '?') s with the lock; $(grep -ho 'SWC region .*\|SWC WORLD .*' "${editor_log}" | tail -2 | tr '\n' ' ')"
        unset SWC_WORLD_FORCE
        if [[ ${status} -ne 0 ]]; then
            cp "${editor_log}" "${SWC_WORK}/logs/build-world-${region}-failed.log" 2>/dev/null || true
        fi
        sleep 15   # another track that waits on the lock gets it now
        in_level "${region}" && break
        [[ ${status} -eq 0 ]] || break   # a failure: the next region (a later run tries this region again)
    done
done
