#!/usr/bin/env bash
# Builds the open world level (/SwcGameContent/World/NewWorld) region by region with no window
# (unreal/SoulwardenClient/Scripts/build_world.py). Each call adds one region to the level and holds import.lock (the
# level is in the shared plugin); the script waits 15 s between two calls, so that an import of another track gets
# the lock. Usage: tools/build-world.sh NAME... | --playable | --all  [--force: make the level again]
#   [--import: before each region, tools/import-world.sh REGION (its meshes, textures, materials)]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
"${SWC_REPO}/tools/content-link.sh"
regions=()
import=0
for argument in "$@"; do
    case "${argument}" in
        --force) export SWC_WORLD_FORCE=1 ;;
        --import) import=1 ;;
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
for region in "${regions[@]}"; do
    [[ ${import} -eq 1 ]] && "${SWC_REPO}/tools/import-world.sh" "${region}"
    if [[ -z "${SWC_WORLD_FORCE:-}" && -f "${SWC_GAME_CONTENT}/world-state.json" ]] && \
        python3 -B -c "import json,sys; sys.exit(0 if sys.argv[2] in json.load(open(sys.argv[1]))['regions'] else 1)" \
            "${SWC_GAME_CONTENT}/world-state.json" "${region}"; then
        echo "$(date -u +%H:%M:%S) ${region}: in the level"
        continue
    fi
    status=0
    SWC_WORLD_REGIONS="${region}" flock -w 3600 -o -E 75 "${SWC_WORK}/unreal/import.lock" \
        systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-16G}" \
        nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
        -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/build_world.py" \
        -AllowCommandletRendering -RenderOffscreen -unattended -nosplash -nosound -nopause -stdout \
        > "${SWC_WORK}/logs/build-world.log" 2>&1 || status=$?
    echo "$(date -u +%H:%M:%S) ${region}: status ${status}; $(grep -ho 'SWC region .*' "${SWC_WORK}/logs/build-world.log" | tail -1)"
    unset SWC_WORLD_FORCE
    sleep 15   # another track that waits on the lock gets it now
done
