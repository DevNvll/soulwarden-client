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
# The world lock (docs/WORLD.md): the client holds it shared for its run (flock -o: the client does not get the
# descriptor). A wait for a level build or an import ends after SWC_WORLD_RUN_WAIT seconds.
source "${SWC_REPO}/tools/lib/world-lock.sh"
mkdir -p "${SWC_WORK}/unreal"
world_read true || { echo "flythrough: the world lock was not free (a level build or an import)" >&2; exit 3; }
source "${SWC_REPO}/tools/lib/gpu-gate.sh"   # then a run slot and the GPU memory (AGENTS.md, the run rules)
run_slot_take flythrough || exit 3
gpu_wait flythrough || exit 3
slot_lock="${SWC_WORK}/unreal/run-slot-${SWC_RUN_SLOT}.lock"
if ! omarchy-agent-desktop status "${desktop_id}" >/dev/null 2>&1; then
    omarchy-agent-desktop create "Soulwarden client" --id "${desktop_id}" --mode "${SWC_DESKTOP_MODE:-1280x720@60}" >/dev/null
fi
# The marks of a development run: tools/dev-stop.sh stops it (SWC_DEV_RUN=1 and the own data folder).
omarchy-agent-desktop run --wait 180 --json "${desktop_id}" -- \
    env SWC_DEV_RUN=1 SWC_RUN=development "SWC_SESSION_DATA=${SWC_DEV_DATA}" \
    gamescope --backend wayland -W "${game_size%x*}" -H "${game_size#*x}" -w "${game_size%x*}" -h "${game_size#*x}" -r 60 -- \
    flock -s -w "${SWC_WORLD_RUN_WAIT:-900}" -o "${SWC_WORLD_LOCK}" flock -s -w 30 -o "${slot_lock}" \
    "${SWC_CLIENT_EXE}" "${SWC_UPROJECT}" -game -nosound -windowed -ResX="${game_size%x*}" -ResY="${game_size#*x}" \
    -SwcFlythrough="${run_plan}" -abslog="${log}" ${SWC_FLY_EXTRA:+"${SWC_FLY_EXTRA}"} > "${out}/launch.json" 2>&1 6>&- || true
run_slot_release   # the client holds the slot (flock above) for its run
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
