#!/usr/bin/env bash
# The development routes of the backend of a development run of this project (tools/lib/dev_world.py): spawn, kill, and
# hurt creatures near the development character. The run needs SWC_SESSION_BACKEND_ARGS=--dev-commands. Never for the data
# folder of the user.
# Usage: tools/creature-world.sh characters | creatures [RANGE] | kinds [TEXT] | spawn KIND [DX DY DZ] | kill ID | ...
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
unset SOULWARDEN_DEV_RUN
exec python3 -B "${SWC_REPO}/tools/lib/dev_world.py" "$@"
