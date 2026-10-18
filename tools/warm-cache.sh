#!/usr/bin/env bash
# Fills the file store of the derived data cache (the only store of the development runs: they cannot reach Zen)
# with the derived data of regions: the world level and the meshes of each region, with their materials and textures
# (unreal/SoulwardenClient/Scripts/warm_cache.py, the graph SWC_EDITOR_DDC: a Zen hit fills the file store). An import
# and a level build with SWC_EDITOR_DDC fill it by themselves; this tool is for content made before (docs/WORLD.md).
# Each call holds import.lock and stops after about 5 minutes of work; the script calls again until the regions are
# done, and waits 15 s between two calls, so that an import of another track gets the lock.
# Usage: tools/warm-cache.sh NAME... | --level (the regions in the level)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
"${SWC_REPO}/tools/content-link.sh"
if [[ "${1:-}" == "--level" ]]; then
    mapfile -t regions < <(python3 -B -c "import json,sys; print('\n'.join(json.load(open(sys.argv[1]))['regions']))" \
        "${SWC_GAME_CONTENT}/world-state.json")
else
    regions=("$@")
fi
[[ ${#regions[@]} -gt 0 ]] || { echo "usage: tools/warm-cache.sh NAME... | --level" >&2; exit 1; }
mkdir -p "${SWC_WORK}/logs"
editor_log="${SWC_REPO}/unreal/SoulwardenClient/Saved/Logs/SoulwardenClient.log"
for attempt in $(seq 1 50); do
    status=0
    SWC_WARM_REGIONS="${regions[*]}" flock -w 3600 -o -E 75 "${SWC_WORK}/unreal/import.lock" \
        systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
        nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
        -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/warm_cache.py" "${SWC_EDITOR_DDC}" \
        -AllowCommandletRendering -RenderOffscreen -unattended -nosplash -nosound -nopause -stdout \
        > "${SWC_WORK}/logs/warm-cache.log" 2>&1 || status=$?
    report="$(grep -ho 'SWC WARM .*' "${editor_log}" | tail -1)"
    echo "$(date -u +%H:%M:%S) warm call ${attempt}: status ${status}; ${report:-no report}"
    sleep 15   # another track that waits on the lock gets it now
    [[ ${status} -eq 0 && "${report}" == *'"incomplete": false'* ]] && exit 0
    [[ ${status} -eq 0 && -n "${report}" ]] || exit 1
done
