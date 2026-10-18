#!/usr/bin/env bash
# Converts the player character (the modular body, the creation databases, the armor appearances) into
# SWC_CONTENT/player (outside the repository). See converter/swc_convert/player.py.
# Usage: tools/convert-player.sh tables | locomotion | camera | weapons | base [--force] | wanted [FILE] | part GENDER SKIN [MTL]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
exec nice -n 10 python3 -B -m swc_convert.player "$@"
