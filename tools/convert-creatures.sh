#!/usr/bin/env bash
# Converts the creatures of the backend data (their bodies, skeletons, clips, and fragments) into SWC_CONTENT/creatures
# (outside the repository). See converter/swc_convert/creature.py and docs/CREATURES.md.
# Usage: tools/convert-creatures.sh census | convert [--near X Y R] [--slices TEXT...] [--all] [--force] | wanted [FILE] | effects
#        | markers
# The conversion runs with nice 10 and at most SWC_MEMORY_MAX (default 8G).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
exec systemd-run --user --scope -q -p MemoryMax="${SWC_MEMORY_MAX:-8G}" nice -n 10 python3 -B -m swc_convert.creature "$@"
