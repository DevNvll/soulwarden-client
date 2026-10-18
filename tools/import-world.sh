#!/usr/bin/env bash
# Imports the meshes of regions (and their textures and materials) into the shared plugin in short calls of
# tools/import.sh: each call stops after about 8 minutes (--max-seconds) and releases import.lock, and the script waits
# 15 s before the next call, so that an import of another track gets the lock between two calls.
# Usage: tools/import-world.sh NAME... | --playable | --all
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
regions=("$@")
if [[ "${1:-}" == "--all" || "${1:-}" == "--playable" ]]; then
    which="${1#--}"
    mapfile -t regions < <(cd "${SWC_REPO}/converter" && python3 -B -c "
import sys
from swc_convert import census
print('\n'.join(census.world_regions() if sys.argv[1] == 'all' else census.playable_regions()))" "${which}")
fi
[[ ${#regions[@]} -gt 0 ]] || { echo "usage: tools/import-world.sh NAME... | --playable | --all" >&2; exit 1; }
for region in "${regions[@]}"; do
    # A region with all its meshes in the import state (same hash) needs no call.
    left=$(python3 -B -c "
import json, os, sys
content, plugin, region = sys.argv[1:4]
meshes = json.load(open(os.path.join(content, 'regions', region, 'meshes.json')))
assets = json.load(open(os.path.join(content, 'manifest.json')))['assets']
path = os.path.join(plugin, 'import-state.json')
state = json.load(open(path)) if os.path.isfile(path) else {}
left = sum(1 for m in meshes if assets.get(m, {}).get('kind') == 'static' and state.get(m) != assets[m].get('hash'))
left += 1 if 'texture:macro:' + region in assets and 'texture:macro:' + region not in state else 0   # the terrain map
left += sum(1 for k, e in assets.items() if k.startswith('impostor:' + region + ':') and state.get(k) != e.get('hash'))
print(left)" \
        "${SWC_CONTENT}" "${SWC_GAME_CONTENT}" "${region}")
    if [[ "${left}" == "0" ]]; then
        echo "$(date -u +%H:%M:%S) ${region}: up to date"
        continue
    fi
    for attempt in $(seq 1 100); do
        status=0
        "${SWC_REPO}/tools/import.sh" --regions "${region}" --max-seconds "${SWC_IMPORT_CALL_SECONDS:-480}" --wait 3600 \
            > "${SWC_WORK}/logs/import-world-last.json" 2>&1 || status=$?
        summary=$(python3 -B -c "
import json, sys
try:
    r = json.load(open(sys.argv[1]))
except Exception:
    print('no report'); sys.exit(0)
print(json.dumps({'imported': r['imported'], 'failed': r['failed'], 'incomplete': r.get('incomplete', False)}))" "${SWC_CONTENT}/import-report.json")
        echo "$(date -u +%H:%M:%S) ${region} call ${attempt} status ${status}: ${summary}"
        sleep 15   # another track that waits on the lock gets it now
        [[ "${summary}" == *'"incomplete": false'* ]] && break
        [[ "${summary}" == "no report" ]] && { echo "import-world: no report for ${region}; see ${SWC_WORK}/logs/import.log" >&2; exit 1; }
    done
done
