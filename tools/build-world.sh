#!/usr/bin/env bash
# Builds the open world level (/SwcGameContent/World/NewWorld) region by region with no window
# (unreal/SoulwardenClient/Scripts/build_world.py). Each call holds the world lock exclusively (no run of the client
# streams the level while a call writes it; tools/lib/world-lock.sh) and import.lock (the level is in the shared plugin)
# and stops after about 4 minutes of work (SWC_WORLD_MAX_SECONDS; a region has three steps); the script calls again
# until the region is in the level, and waits 15 s between two calls, so that an import of another track gets the
# lock. Each call writes one line to the output: the status, the seconds with the lock, and the last region line.
# The editor log of a failed call is kept in SWC_WORK/logs/build-world-REGION-failed.log.
# Usage: tools/build-world.sh NAME... | --playable | --all  [--force: make the level again]
#   [--import: before each region, tools/import-world.sh REGION (its meshes, textures, materials)]
#   [--cells: a region in the level gets new cell actors when its cells version is old (build_world.py CELLS_VERSION)]
#   [--terrain: a region in the level gets its Landscape again with the target layers of its terrain plan (T1;
#    build_world.py TERRAIN_VERSION; tools/import-terrain.sh first)]
#   [--far-grid: only the far grid step (the Landscape proxies and the far mesh groups into the runtime grid SwcFar of
#    build_world.py; SWC_WORLD_FAR_RANGE cm, default 20 km), one call with no region]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
"${SWC_REPO}/tools/content-link.sh"
source "${SWC_REPO}/tools/lib/world-lock.sh"
regions=()
import=0
far_only=0
for argument in "$@"; do
    case "${argument}" in
        --force) export SWC_WORLD_FORCE=1 ;;
        --import) import=1 ;;
        --cells) export SWC_WORLD_CELLS=1 ;;
        --terrain) export SWC_WORLD_TERRAIN=1 ;;
        --far-grid) far_only=1 ;;
        --all|--playable)
            mapfile -t -O "${#regions[@]}" regions < <(cd "${SWC_REPO}/converter" && python3 -B -c "
import sys
from swc_convert import census
print('\n'.join(census.world_regions() if sys.argv[1] == 'all' else census.playable_regions()))" "${argument#--}") ;;
        *) regions+=("${argument}") ;;
    esac
done
[[ ${#regions[@]} -gt 0 || ${far_only} -eq 1 ]] || { echo "usage: tools/build-world.sh NAME... | --playable | --all [--force]" >&2; exit 1; }
mkdir -p "${SWC_WORK}/logs"
in_level() {   # with --cells or --terrain: in the level with the current rule (CELLS_VERSION 5, TERRAIN_VERSION 2)
    [[ -f "${SWC_GAME_CONTENT}/world-state.json" ]] && \
        python3 -B -c "import json,sys; r = json.load(open(sys.argv[1]))['regions'].get(sys.argv[2]); sys.exit(0 if r is not None and (sys.argv[3] != '1' or r.get('cells version', 1) >= 5) and (sys.argv[4] != '1' or r.get('terrain version', 1) >= 2) else 1)" \
            "${SWC_GAME_CONTENT}/world-state.json" "$1" "${SWC_WORLD_CELLS:-}" "${SWC_WORLD_TERRAIN:-}"
}
editor_log="${SWC_REPO}/unreal/SoulwardenClient/Saved/Logs/SoulwardenClient.log"
# The Landscape step needs the renderer (the edit layers of the Landscape use the GPU). The cells again need no renderer:
# with it, each chunk compiled the shader maps and built the textures of its new materials (call 9 of the final pass:
# 106 shader maps, a chunk of 8 actors in 73 to 201 s). The bounds of a cell actor come from the saved bounds of its
# meshes (build_world.py checks them).
render=(-AllowCommandletRendering -RenderOffscreen)
[[ -n "${SWC_WORLD_CELLS:-}" && -z "${SWC_WORLD_TERRAIN:-}" ]] && render=(-nullrhi)
# One call of build_world.py for the regions (SWC_WORLD_REGIONS) with import.lock; sets `status`, writes one line.
build_call() {   # build_call REGIONS LABEL
    status=0
    rm -f "${SWC_WORK}/logs/build-world-seconds"
    SWC_WORLD_REGIONS="$1" world_write -w 3600 flock -w 3600 -o -E 75 "${SWC_WORK}/unreal/import.lock" \
        bash -c 'out="$1"; shift; start=${SECONDS}; status=0; "$@" || status=$?; echo $((SECONDS - start)) > "${out}"; exit ${status}' _ \
        "${SWC_WORK}/logs/build-world-seconds" \
        systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-16G}" \
        nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
        -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/build_world.py" "${SWC_EDITOR_DDC}" \
        "${render[@]}" -unattended -nosplash -nosound -nopause -stdout \
        > "${SWC_WORK}/logs/build-world.log" 2>&1 || status=$?
    echo "$(date -u +%H:%M:%S) $2: status ${status}, $(cat "${SWC_WORK}/logs/build-world-seconds" 2>/dev/null || echo '?') s with the lock; $(grep -ho 'SWC region .*\|SWC WORLD .*' "${editor_log}" | tail -2 | cut -c1-200 | tr '\n' ' ')"
    if [[ ${status} -ne 0 ]]; then
        cp "${editor_log}" "${SWC_WORK}/logs/build-world-failed.log" 2>/dev/null || true
    fi
    sleep 15   # another track that waits on the lock gets it now
}
if [[ ${far_only} -eq 1 ]]; then
    render=(-nullrhi)
    build_call "" "far grid"
    exit "${status}"
fi
if [[ -n "${SWC_WORLD_CELLS:-}" || -n "${SWC_WORLD_TERRAIN:-}" ]]; then
    # The cells or the terrain again: each call works on the regions that are left, in order, until its time limit.
    previous=""
    for attempt in $(seq 1 300); do
        left=()
        for region in "${regions[@]}"; do in_level "${region}" || left+=("${region}"); done
        [[ ${#left[@]} -eq 0 ]] && break
        progress="$(md5sum < "${SWC_GAME_CONTENT}/world-state.json" 2>/dev/null)"
        if [[ "${progress}" == "${previous}" ]]; then
            # A call that changed nothing in the level state (for example a region with no material instance, or a
            # wrong region name): stop, so that the calls do not hold the world lock again and again.
            echo "build-world: the last call made no progress (${#left[@]} regions left, from ${left[0]}); see ${editor_log}" >&2
            exit 1
        fi
        previous="${progress}"
        build_call "${left[*]}" "$([[ -n "${SWC_WORLD_TERRAIN:-}" ]] && echo terrain || echo cells) call ${attempt} (${#left[@]} regions left, from ${left[0]})"
        [[ ${status} -eq 0 ]] || break
    done
    exit 0
fi
for region in "${regions[@]}"; do
    [[ ${import} -eq 1 ]] && "${SWC_REPO}/tools/import-world.sh" "${region}"
    if [[ -z "${SWC_WORLD_FORCE:-}" ]] && in_level "${region}"; then
        echo "$(date -u +%H:%M:%S) ${region}: in the level"
        continue
    fi
    for attempt in $(seq 1 10); do
        status=0
        rm -f "${SWC_WORK}/logs/build-world-seconds"
        SWC_WORLD_REGIONS="${region}" world_write -w 3600 flock -w 3600 -o -E 75 "${SWC_WORK}/unreal/import.lock" \
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
