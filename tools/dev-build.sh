#!/usr/bin/env bash
# A development session with no client that gives a character of the development account a build preset of the backend
# (the equipment, the masteries, the tree nodes, the ability bars): the HUD checks need a character with weapons and
# abilities. The development database (SWC_DEV_DATA) keeps the build for the next runs of tools/dev-run.sh.
# Usage: tools/dev-build.sh [--list] [--preset NAME] [--variant NAME] [--character NAME]
# The session is the session of a development run (tools/lib/session.sh: one network namespace with loopback only, the
# lock of SWC_DEV_DATA, the made-up identity, the mark SWC_DEV_RUN=1). Its backend has the switch --dev-commands (the
# development routes; SWC_SESSION_BACKEND_ARGS of session.sh, development sessions only).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
unset SOULWARDEN_DEV_RUN
if [[ "$(realpath -m -- "${SWC_DEV_DATA}")" == "$(realpath -m -- "${SWC_PLAYER_DATA}")" ]]; then
    echo "dev-build: SWC_DEV_DATA is the data folder of the user. A development run never uses it." >&2
    exit 1
fi
mkdir -p "${SWC_DEV_DATA}/logs"
exec env SWC_DEV_RUN=1 SWC_RUN=development SWC_SESSION_BACKEND_ARGS=--dev-commands SWC_STEAM_ID=76561202255233000 "SWC_PERSONA_NAME=Swc Dev" \
    "SWC_SESSION_DATA=${SWC_DEV_DATA}" "SWC_SESSION_LOCK=${SWC_DEV_DATA}/run.lock" "SWC_SESSION_SEED=Devwarden" \
    "${SWC_REPO}/tools/lib/session.sh" -- python3 -B "${SWC_REPO}/tools/lib/dev_build.py" "$@" -- true
