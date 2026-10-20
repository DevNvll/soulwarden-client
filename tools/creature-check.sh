#!/usr/bin/env bash
# A video check of the creatures (docs/CREATURES.md): a development run with the client window on the own agent desktop
# (SWC_AGENT_DESKTOP), the development routes of the backend (a script of tools/lib/dev_world.py), an optional drive script
# of the player, and a recording of the headless output of that desktop (wf-recorder).
# Usage: tools/creature-check.sh LABEL STAY ROUTES [DRIVE] [-- CLIENT ARGUMENTS]
#   LABEL    the name of the video and of the logs in SWC_WORK/evidence/STAGE (SWC_STAGE, default 1.9)
#   STAY     the seconds in the world (-SwcStay); the client then leaves and stops
#   ROUTES   a script of the development routes ("SECONDS COMMAND WORDS..." from the world entry)
#   DRIVE    a drive script of the player controller (-SwcDrive)
# SWC_DEV_CHARACTER: the character of the run and of the routes (default: the first one). SWC_DEV_PLACE (X:Y:Z, engine m):
# its place at the start of the run. SWC_DEV_SPEC: more keys of the create spec of tools/lib/dev_character.py (gear, unequip).
# Never for the data folder of the user (tools/dev-run.sh and tools/lib/dev_world.py refuse it).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
unset SOULWARDEN_DEV_RUN
label="$1"; stay="$2"; routes="$3"; shift 3
drive=""
if [[ $# -gt 0 && "$1" != "--" ]]; then drive="$1"; shift; fi
[[ "${1:-}" == "--" ]] && shift
export SWC_SESSION_BACKEND_ARGS="--dev-commands"
desktop="${SWC_AGENT_DESKTOP:-soulwarden-client-dev}"
evidence="${SWC_WORK}/evidence/${SWC_STAGE:-1.9}"
log="${SWC_WORK}/logs/dev-run/${label}-client.log"
mkdir -p "${evidence}"
rm -f "${log}"
# The debug text lines: the frames of a video are found by them (off by default from stage WF7). SWC_DEBUG_LINES=0: no debug text
# (the videos of the normal look).
# SWC_PLATES: the name plates of the creatures (default all; none: the videos of the retail compare, with -SwcUi=off as F10 there).
client_arguments=(-SwcPlates="${SWC_PLATES:-all}")
[[ "${SWC_DEBUG_LINES:-1}" == 1 ]] && client_arguments+=(-SwcDebugLines)
[[ -n "${drive}" ]] && client_arguments+=(-SwcDrive="$(realpath "${drive}")")
enter=(--enter)
[[ -n "${SWC_DEV_CHARACTER:-}" ]] && enter=(--enter "${SWC_DEV_CHARACTER}")
# The same start place for each run: the swings of a check move the character, and the database keeps its place.
[[ -n "${SWC_DEV_CHARACTER:-}" && -n "${SWC_DEV_PLACE:-}" ]] && enter+=(--create "name=${SWC_DEV_CHARACTER},place=${SWC_DEV_PLACE}${SWC_DEV_SPEC:+,${SWC_DEV_SPEC}}")
"${SWC_REPO}/tools/dev-run.sh" --window "${enter[@]}" --quit-when world --stay "${stay}" --label "${label}" -- "${client_arguments[@]}" "$@" >/dev/null
for ((i = 0; i < 240; ++i)); do
    grep -q 'world: own entity' "${log}" 2>/dev/null && break
    sleep 1
done
if ! grep -q 'world: own entity' "${log}" 2>/dev/null; then
    echo "creature-check: no world entry in 240 s (${log})" >&2
    "${SWC_REPO}/tools/dev-stop.sh" >/dev/null 2>&1 || true
    exit 1
fi
# The recording: wf-recorder on the headless output of the own agent desktop (never an output of the user).
output=$(omarchy-agent-desktop status --json "${desktop}" | python3 -c "import json,sys; print(json.load(sys.stdin)['desktop']['output'])")
if [[ ! "${output}" =~ ^AGENT-[0-9]+$ ]]; then
    echo "creature-check: the desktop ${desktop} has no agent output (${output})" >&2
    "${SWC_REPO}/tools/dev-stop.sh" >/dev/null 2>&1 || true
    exit 1
fi
# The wall time of the start of the recording (the side-by-side videos of stage P3.C: tools/lib/creature_compare.py --video).
date +%s.%N >"${evidence}/${label}-video-start.txt"
wf-recorder -y -o "${output}" -c libx264 -r 30 -f "${evidence}/${label}.mp4" >"${SWC_WORK}/logs/dev-run/${label}-recorder.log" 2>&1 &
recorder=$!
# With a drive script, the routes go on the clock of the drive (its lines "drive: SECONDS s" in the client log): the times
# of the two scripts then agree.
clock=()
[[ -n "${drive}" ]] && clock=(--clock "${log}")
python3 -B "${SWC_REPO}/tools/lib/dev_world.py" script "${routes}" "${clock[@]}" > "${evidence}/${label}-routes.txt" 2>&1 || true
for ((i = 0; i < stay + 120; ++i)); do
    grep -q 'client: stop' "${log}" 2>/dev/null && break
    sleep 1
done
sleep 2
kill -INT "${recorder}" 2>/dev/null || true
wait "${recorder}" 2>/dev/null || true
"${SWC_REPO}/tools/dev-stop.sh" >/dev/null 2>&1 || true
cp "${log}" "${evidence}/${label}-client.log"
cp "${SWC_DEV_DATA}/logs/backend.log" "${evidence}/${label}-backend.log"
echo "creature-check: ${evidence}/${label}.mp4"
