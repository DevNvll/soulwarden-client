#!/usr/bin/env bash
# The in-game scenes of the quests (stage Q5, AST-240; docs/QUESTS.md "The scenes, the cinematics, and the whispers"): the cutscene
# spawners of the capitals, the scene slices and their timelines (SWC_CONTENT/quests/scenes.json and quests/scenes/NAME.json). See
# converter/swc_convert/scenes.py.
# Usage: tools/convert-scenes.sh write [--only NAME] | convert [NAME...] [--force] (the actors and clips; then tools/import-player.sh and
# tools/import.sh)
# The conversion runs with nice 10 and at most SWC_MEMORY_MAX (default 8G).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
exec systemd-run --user --scope -q --unit="swc-scenes-$(date +%s%N)" -p MemoryMax="${SWC_MEMORY_MAX:-8G}" nice -n 10 python3 -B -m swc_convert.scenes "$@"
