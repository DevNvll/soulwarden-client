#!/usr/bin/env bash
# The matched views of the fidelity checks (docs/ENVIRONMENT.md, "The matched views"): the views of a set of retail references in one
# development run, each with the feet, the camera, the field of view, and the world hour of its retail picture; for each view the
# client picture and a side-by-side picture (retail | client), a contact sheet, a report, and the compare of the client clock with
# the server. See tools/lib/match_view.py.
# Usage: tools/match-view.sh SET [VIEW...] [--out FOLDER] [--label LABEL] [--settle S] [--lead S] [--hours VIEW=HH:MM,...]
#   [--variant VIEW@NAME=COMMAND;...] [--client-args ARG...]
# SWC_DEV_CHARACTER: the character of the run (a development character). Never for the data folder of the user (tools/dev-run.sh
# and tools/lib/dev_world.py refuse it).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
unset SOULWARDEN_DEV_RUN
exec python3 -B "${SWC_REPO}/tools/lib/match_view.py" "$@"
