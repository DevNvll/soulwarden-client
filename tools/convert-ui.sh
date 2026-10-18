#!/usr/bin/env bash
# Converts the UI content of the game copy (canvases and UI slices as data, the Lua scripts, the textures and sprites,
# the fonts, the English texts) into SWC_CONTENT/ui (outside the repository). See converter/swc_convert/ui.py.
# Usage: tools/convert-ui.sh hud [--force] | canvases [PATH...|--all] | scripts | textures ... | wanted [FILE] | fonts | text | items | uistates
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
exec nice -n 10 systemd-run --user --scope -q -p MemoryMax=8G python3 -B -m swc_convert.ui "$@"
