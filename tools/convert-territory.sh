#!/usr/bin/env bash
# The data of the territory standing cards (QG1; docs/UI.md "The territory standing"): the standings, the cards of each rank row, and the
# card values and UI columns (SWC_CONTENT/ui/gamedata/territory.json). See converter/swc_convert/territorycards.py.
# Usage: tools/convert-territory.sh
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
exec nice -n 10 python3 -B -m swc_convert.territorycards "$@"
