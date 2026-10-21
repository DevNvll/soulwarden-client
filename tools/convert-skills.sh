#!/usr/bin/env bash
# The data of the character window (stage MVP-G, docs/UI.md "The character window"): the weapon mastery trees, the rank tables, and
# the trade skills (SWC_CONTENT/ui/gamedata/skilltrees.json). See converter/swc_convert/skilltrees.py.
# Usage: tools/convert-skills.sh
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
exec nice -n 10 python3 -B -m swc_convert.skilltrees "$@"
