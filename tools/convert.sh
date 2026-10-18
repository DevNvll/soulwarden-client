#!/usr/bin/env bash
# Converts content of the game copy into SWC_CONTENT (outside the repository). See converter/swc_convert/__main__.py.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
exec nice -n 10 python3 -B -m swc_convert "$@"
