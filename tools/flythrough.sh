#!/usr/bin/env bash
# The fly-through of the open world level (stage 1.3 evidence): the client (no backend) on the world level flies a
# camera along a plan (converter/swc_convert/terrain.py flythrough_plan) in a gamescope window on the own agent desktop
# (SWC_AGENT_DESKTOP), logs the frame rate and the streaming, and ends. --frames EVERY writes a picture every EVERY
# frames, then a video (ffmpeg) of them, and deletes the pictures.
# Usage: tools/flythrough.sh PLAN.json OUTPUT_FOLDER [--frames EVERY]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
plan="$(realpath "${1:?usage: tools/flythrough.sh PLAN.json OUTPUT_FOLDER [--frames EVERY]}")"
out="$(realpath -m "${2:?usage: tools/flythrough.sh PLAN.json OUTPUT_FOLDER [--frames EVERY]}")"
every="${4:-}"
[[ "${3:-}" == "--frames" && -n "${every}" ]] || every=""
desktop_id="${SWC_AGENT_DESKTOP:-soulwarden-client-dev}"
[[ "${desktop_id}" =~ ^soulwarden-client-[a-z0-9-]+$ ]] || { echo "flythrough: SWC_AGENT_DESKTOP must start with soulwarden-client-" >&2; exit 2; }
game_size="${SWC_GAME_SIZE:-1256x660}"
mkdir -p "${out}"
"${SWC_REPO}/tools/content-link.sh"
run_plan="${out}/plan.json"
frames="${out}/frames"
if [[ -n "${every}" ]]; then
    rm -rf "${frames}"; mkdir -p "${frames}"
    python3 -B -c "import json,sys; p=json.load(open(sys.argv[1])); p['frames']={'folder': sys.argv[2], 'every': int(sys.argv[3])}; json.dump(p, open(sys.argv[4], 'w'))" \
        "${plan}" "${frames}" "${every}" "${run_plan}"
else
    python3 -B -c "import json,sys; p=json.load(open(sys.argv[1])); p.pop('frames', None); json.dump(p, open(sys.argv[2], 'w'))" "${plan}" "${run_plan}"
fi
log="${out}/client.log"
rm -f "${log}"
if ! omarchy-agent-desktop status "${desktop_id}" >/dev/null 2>&1; then
    omarchy-agent-desktop create "Soulwarden client" --id "${desktop_id}" --mode "${SWC_DESKTOP_MODE:-1280x720@60}" >/dev/null
fi
# The marks of a development run: tools/dev-stop.sh stops it (SWC_DEV_RUN=1 and the own data folder).
omarchy-agent-desktop run --wait 180 --json "${desktop_id}" -- \
    env SWC_DEV_RUN=1 SWC_RUN=development "SWC_SESSION_DATA=${SWC_DEV_DATA}" \
    gamescope --backend wayland -W "${game_size%x*}" -H "${game_size#*x}" -w "${game_size%x*}" -h "${game_size#*x}" -r 60 -- \
    "${SWC_CLIENT_EXE}" "${SWC_UPROJECT}" -game -nosound -windowed -ResX="${game_size%x*}" -ResY="${game_size#*x}" \
    -SwcFlythrough="${run_plan}" -abslog="${log}" ${SWC_FLY_EXTRA:+"${SWC_FLY_EXTRA}"} > "${out}/launch.json" 2>&1 || true
for _ in $(seq 1 1800); do
    grep -q "SWC FLYTHROUGH\|Critical error" "${log}" 2>/dev/null && break
    sleep 1
done
sleep 5
grep -h "SWC FLYTHROUGH\|Critical error" "${log}" | cut -c30- || echo "flythrough: no summary in ${log}" >&2
if [[ -n "${every}" && -n "$(ls "${frames}" 2>/dev/null | head -1)" ]]; then
    rate="60/${every}"   # a fraction: EVERY can be more than 60 (an integer division gave 0)
    ffmpeg -loglevel error -y -framerate "${rate}" -pattern_type glob -i "${frames}/*.png" -vf fps=30 \
        -c:v libx264 -pix_fmt yuv420p -crf 23 -preset medium "${out}/flythrough.mp4"
    echo "video: ${out}/flythrough.mp4 ($(ls "${frames}" | wc -l) pictures, ${rate} pictures each second)"
    rm -rf "${frames}"
fi
