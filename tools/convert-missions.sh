#!/usr/bin/env bash
# The mission data of the boards (C-12; docs/UI.md "The mission boards"): the town filter values of the C++ offer generator, the mission rows,
# the projects, the events, the weights, the reward modifiers (SWC_CONTENT/ui/gamedata/missions.json). See converter/swc_convert/missions.py.
# Usage: tools/convert-missions.sh
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
exec nice -n 10 python3 -B -m swc_convert.missions "$@"
