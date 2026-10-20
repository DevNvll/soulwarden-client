#!/usr/bin/env bash
# A video check of the front end (docs/FRONTEND.md): a development run with the client window on the own agent desktop
# (SWC_AGENT_DESKTOP) and no -SwcPlay: the landing screen of the game shows the character list. A UI drive (-SwcUiDrive=FILE
# in the client arguments; its clock starts at the landing screen) selects a character and presses PLAY. The recording is of
# the headless output of that desktop (wf-recorder), as tools/creature-check.sh.
# Usage: tools/menu-check.sh LABEL SECONDS [-- CLIENT ARGUMENTS]
#   LABEL    the name of the video and of the logs in SWC_WORK/evidence/STAGE (SWC_STAGE, default F2)
#   SECONDS  the client stops after this time (-SwcQuitAfter)
# SWC_DEV_CHARACTER and SWC_DEV_PLACE (X:Y:Z, engine m): the place of that character at the start (as tools/creature-check.sh).
# Never for the data folder of the user (tools/dev-run.sh refuses it).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
unset SOULWARDEN_DEV_RUN
label="$1"; seconds="$2"; shift 2
[[ "${1:-}" == "--" ]] && shift
desktop="${SWC_AGENT_DESKTOP:-soulwarden-client-dev}"
evidence="${SWC_WORK}/evidence/${SWC_STAGE:-F2}"
log="${SWC_WORK}/logs/dev-run/${label}-client.log"
mkdir -p "${evidence}"
rm -f "${log}"
create=()
if [[ -n "${SWC_DEV_CHARACTER:-}" && -n "${SWC_DEV_PLACE:-}" ]]; then
    # The place route is a development command of the backend.
    export SWC_SESSION_BACKEND_ARGS="--dev-commands"
    create=(--create "name=${SWC_DEV_CHARACTER},place=${SWC_DEV_PLACE}")
fi
"${SWC_REPO}/tools/dev-run.sh" --window "${create[@]}" --label "${label}" -- -SwcQuitAfter="${seconds}" "$@" \
    >"${SWC_WORK}/logs/dev-run/${label}-launch.log" 2>&1
for ((i = 0; i < 240; ++i)); do
    grep -q 'the front end: the landing screen' "${log}" 2>/dev/null && break
    grep -q 'client: stop' "${log}" 2>/dev/null && break
    sleep 1
done
if ! grep -q 'the front end: the landing screen' "${log}" 2>/dev/null; then
    echo "menu-check: no landing screen in 240 s (${log})" >&2
    "${SWC_REPO}/tools/dev-stop.sh" >/dev/null 2>&1 || true
    exit 1
fi
output=$(omarchy-agent-desktop status --json "${desktop}" | python3 -c "import json,sys; print(json.load(sys.stdin)['desktop']['output'])")
if [[ ! "${output}" =~ ^AGENT-[0-9]+$ ]]; then
    echo "menu-check: the desktop ${desktop} has no agent output (${output})" >&2
    "${SWC_REPO}/tools/dev-stop.sh" >/dev/null 2>&1 || true
    exit 1
fi
date +%s.%N >"${evidence}/${label}-video-start.txt"
wf-recorder -y -o "${output}" -c libx264 -r 30 -f "${evidence}/${label}.mp4" >"${SWC_WORK}/logs/dev-run/${label}-recorder.log" 2>&1 &
recorder=$!
for ((i = 0; i < seconds + 120; ++i)); do
    grep -q 'client: stop' "${log}" 2>/dev/null && break
    sleep 1
done
sleep 2
kill -INT "${recorder}" 2>/dev/null || true
wait "${recorder}" 2>/dev/null || true
cp "${log}" "${evidence}/${label}-client.log" 2>/dev/null || true
echo "menu-check: ${evidence}/${label}.mp4"
