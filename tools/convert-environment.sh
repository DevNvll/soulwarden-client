#!/usr/bin/env bash
# Reads the environment of the world from the game copy into SWC_CONTENT/environment (stage E0, docs/ENVIRONMENT.md;
# converter/swc_convert/environment.py): the main-world mission, the time presets, the environment entities of each region (the
# local lights and probes, the time-of-day, fog, rain, snow, weather, ocean, water, and river volumes with their shapes), and the
# sky slices.
# Usage: tools/convert-environment.sh write [--regions NAME...] | volumes X Y Z | sample PRESET HOUR [NAME...] | sky | snow | check
# The conversion runs with nice 10 and at most SWC_MEMORY_MAX (default 8G).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
if [[ "${1:-}" == "check" ]]; then
    shift
    exec systemd-run --user --scope -q -p MemoryMax="${SWC_MEMORY_MAX:-8G}" nice -n 10 python3 -B -m swc_convert.environment_check "$@"
fi
exec systemd-run --user --scope -q -p MemoryMax="${SWC_MEMORY_MAX:-8G}" nice -n 10 python3 -B -m swc_convert.environment "$@"
