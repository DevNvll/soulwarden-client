#!/usr/bin/env bash
# The water of the world (stage WF5, docs/WATER.md; converter/swc_convert/water.py) into SWC_CONTENT/water (outside the
# repository): the surfaces of the water volumes and the rivers of each region, the ocean and its overrides, the water materials,
# and the water quadtree of each region. Needs the environment records (tools/convert-environment.sh write).
# Usage: tools/convert-water.sh write|check|samples [REGION...]
#        tools/convert-water.sh bodies write|check [REGION...] [--out DIR]   # the water bodies of the Unreal Water plugin
#                                                                          # (converter/swc_convert/water_bodies.py; after write)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
module=swc_convert.water
if [[ "${1:-}" == "bodies" ]]; then
    module=swc_convert.water_bodies
    shift
fi
exec systemd-run --user --scope -q -p MemoryMax=8G nice -n 10 python3 -B -m "${module}" "$@"
