#!/usr/bin/env bash
# The front end content (docs/FRONTEND.md, stage F4; converter/swc_convert/frontend.py):
#   tools/convert-frontend.sh clips [--force] [--pack menu|creation]   the menu idle clip of each gender, or the clip of the
#                                               creation level (stage Q3; then tools/import-player.sh)
#   tools/convert-frontend.sh scene             SWC_CONTENT/frontend/nighthaven.json: the Player Entity, the menu camera, the sequences
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
exec systemd-run --user --scope -q -p MemoryMax="${SWC_MEMORY_MAX:-8G}" nice -n 10 python3 -B -m swc_convert.frontend "$@"
