#!/usr/bin/env bash
# The persons of the settlements (stage P4.8, docs/PERSONS.md): the persons of the level data with their bodies, items, idle and
# talk timelines, and the line and camera of their conversation (SWC_CONTENT/persons/persons.json), then the conversion of the
# bodies, the items, and the clips with the player pipeline (then tools/import-player.sh). See converter/swc_convert/persons.py.
# Usage: tools/convert-persons.sh write [--near X Y RADIUS] | convert [--force]
# The conversion runs with nice 10 and at most SWC_MEMORY_MAX (default 8G).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
exec systemd-run --user --scope -q -p MemoryMax="${SWC_MEMORY_MAX:-8G}" nice -n 10 python3 -B -m swc_convert.persons "$@"
