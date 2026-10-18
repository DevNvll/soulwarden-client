#!/usr/bin/env bash
# The tests of the converter that need no game data (tests/converter, Python unittest).
# Usage: tools/test-converter.sh [-v]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}"
exec python3 -B -m unittest discover -s tests/converter -p "test_*.py" "$@"
