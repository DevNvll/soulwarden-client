#!/usr/bin/env bash
# A video check of the UI (docs/UI.md): a development run with the client window on the own agent desktop
# (SWC_AGENT_DESKTOP), a script of the harness input of the UI (-SwcUiDrive), and a recording of the headless output of
# that desktop (wf-recorder). The input goes to the client only through its harness input (no input tool of the host).
# Usage: tools/ui-check.sh LABEL STAY DRIVE [-- CLIENT ARGUMENTS]
#   LABEL    the name of the video and of the logs in SWC_WORK/evidence/STAGE (SWC_STAGE, default P2.2)
#   STAY     the seconds in the world (-SwcStay); the client then leaves and stops
#   DRIVE    a script of the harness input of the UI (tests/drive/ui-*.txt)
# Never for the data folder of the user (tools/dev-run.sh refuses it).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
unset SOULWARDEN_DEV_RUN
label="$1"; stay="$2"; drive="$3"; shift 3
[[ "${1:-}" == "--" ]] && shift
desktop="${SWC_AGENT_DESKTOP:-soulwarden-client-dev}"
evidence="${SWC_WORK}/evidence/${SWC_STAGE:-P2.2}"
log="${SWC_WORK}/logs/dev-run/${label}-client.log"
mkdir -p "${evidence}"
rm -f "${log}"
"${SWC_REPO}/tools/dev-run.sh" --window --enter --quit-when world --stay "${stay}" --label "${label}" -- \
    -SwcUiDrive="$(realpath "${drive}")" "$@" >/dev/null
for ((i = 0; i < 240; ++i)); do
    grep -q 'ui: the world entry' "${log}" 2>/dev/null && break
    sleep 1
done
if ! grep -q 'ui: the world entry' "${log}" 2>/dev/null; then
    echo "ui-check: no world entry of the UI in 240 s (${log})" >&2
    "${SWC_REPO}/tools/dev-stop.sh" >/dev/null 2>&1 || true
    exit 1
fi
# The recording: wf-recorder on the headless output of the own agent desktop (never an output of the user).
output=$(omarchy-agent-desktop status --json "${desktop}" | python3 -c "import json,sys; print(json.load(sys.stdin)['desktop']['output'])")
if [[ ! "${output}" =~ ^AGENT-[0-9]+$ ]]; then
    echo "ui-check: the desktop ${desktop} has no agent output (${output})" >&2
    "${SWC_REPO}/tools/dev-stop.sh" >/dev/null 2>&1 || true
    exit 1
fi
wf-recorder -y -o "${output}" -c libx264 -r 30 -f "${evidence}/${label}.mp4" >"${SWC_WORK}/logs/dev-run/${label}-recorder.log" 2>&1 &
recorder=$!
for ((i = 0; i < stay + 120; ++i)); do
    grep -q 'client: stop' "${log}" 2>/dev/null && break
    sleep 1
done
sleep 2
kill -INT "${recorder}" 2>/dev/null || true
wait "${recorder}" 2>/dev/null || true
"${SWC_REPO}/tools/dev-stop.sh" >/dev/null 2>&1 || true
grep -a "ui drive\|ui: the cursor\|ui: ui state\|LogSwcUi: Warning\|LogSwcUi: Error" "${log}" > "${evidence}/${label}-ui.txt" || true
echo "ui-check: ${evidence}/${label}.mp4 (the UI lines: ${evidence}/${label}-ui.txt)"
