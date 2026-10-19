#!/usr/bin/env bash
# A development run (agents): the backend copy and the client in one network namespace with loopback only, on the own
# data folder SWC_DEV_DATA (never the data folder of the user), with a made-up Steam identity and a seed character.
# Usage: tools/dev-run.sh [--window] [--enter [NAME]] [--create SPEC]... [--seed NAME] [--quit-when characters|world] [--stay S]
#                         [--seconds N] [--label TEXT] [-- CLIENT ARGUMENTS]
#   (default)   the client with no window and no sound (-nullrhi -nosound) in the foreground; it stops after
#               --seconds (default 120). Logs: SWC_WORK/logs/dev-run/LABEL-client.log, SWC_DEV_DATA/logs/backend.log.
#   --window    the client window on the own agent desktop "soulwarden-client-dev" or SWC_AGENT_DESKTOP (gamescope, no sound; a window
#               guard keeps it there: tools/lib/window_guard.py). The script
#               returns when the window shows; tools/dev-stop.sh stops the run.
#   --enter     after the character list the client enters the world with the first character (-SwcPlay=first).
#   --enter NAME            the same with the character NAME.
#   --create SPEC           before the client starts, the create request of the retail client makes a character
#                           (tools/lib/dev_character.py: name=NAME,gender=Female,race=face07,hair=hairstyle12m,...).
#   --quit-when characters   the client stops after the character list (status 0) or after a failure (status 1).
#   --quit-when world        the client stays --stay seconds in the world (default 10), leaves, and stops (status 0).
# Each process of a development run has the mark SWC_DEV_RUN=1 (tools/dev-stop.sh). A process of this project never
# has SOULWARDEN_DEV_RUN=1 (the mark of the Soulwarden development runs).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
unset SOULWARDEN_DEV_RUN

window=0
enter=""
creates=()
stay=""
seed="Devwarden"
quit_when=""
seconds=120
label="run"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --window) window=1 ;;
        --enter)
            enter=first
            if [[ $# -gt 1 && "$2" != --* ]]; then enter="$2"; shift; fi ;;
        --create) creates+=("$2"); shift ;;
        --stay) stay="$2"; shift ;;
        --seed) seed="$2"; shift ;;
        --quit-when) quit_when="$2"; shift ;;
        --seconds) seconds="$2"; shift ;;
        --label) label="$2"; shift ;;
        --) shift; break ;;
        *) echo "dev-run: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

if [[ "$(realpath -m -- "${SWC_DEV_DATA}")" == "$(realpath -m -- "${SWC_PLAYER_DATA}")" ]]; then
    echo "dev-run: SWC_DEV_DATA is the data folder of the user. A development run never uses it." >&2
    exit 1
fi
if [[ ! -x "${SWC_BACKEND_EXE}" ]]; then
    echo "dev-run: no backend copy at ${SWC_BACKEND_DIR} (run tools/backend-update.sh)" >&2
    exit 1
fi
mkdir -p "${SWC_DEV_DATA}/logs"
# The client loads the imported content from the shared content plugin (the link of this checkout).
"${SWC_REPO}/tools/content-link.sh"
logs="${SWC_WORK}/logs/dev-run"
mkdir -p "${logs}"
# A new backend log for each run, while no run holds the lock of the data folder.
exec 8>>"${SWC_DEV_DATA}/run.lock"
if ! flock -n 8; then
    echo "dev-run: a development run uses ${SWC_DEV_DATA} now (tools/dev-stop.sh stops it)." >&2
    exit 3
fi
: >"${SWC_DEV_DATA}/logs/backend.log"
exec 8>&-

client_log="${logs}/${label}-client.log"
client_arguments=(-game -nosound -SwcConnect -abslog="${client_log}")
if [[ -n "${quit_when}" ]]; then
    client_arguments+=(-SwcQuitWhen="${quit_when}")
fi
if [[ -n "${enter}" ]]; then
    client_arguments+=(-SwcPlay="${enter}")
fi
# The create requests run in the namespace of the session, before the client starts.
client_prefix=()
if [[ ${#creates[@]} -gt 0 ]]; then
    client_prefix=(python3 -B "${SWC_REPO}/tools/lib/dev_character.py" "${creates[@]}" --)
fi
if [[ -n "${stay}" ]]; then
    client_arguments+=(-SwcStay="${stay}")
fi
# A made-up identity: a Steam id of the range of the made-up ids of the backend tests, not an account of a person.
run_environment=(SWC_DEV_RUN=1 SWC_RUN=development SWC_STEAM_ID=76561202255233000 "SWC_PERSONA_NAME=Swc Dev"
    "SWC_SESSION_DATA=${SWC_DEV_DATA}" "SWC_SESSION_LOCK=${SWC_DEV_DATA}/run.lock" "SWC_SESSION_SEED=${seed}")
# The extra backend arguments of a development session (tools/lib/session.sh) go by name: a window run starts with the
# environment of the compositor.
if [[ -n "${SWC_SESSION_BACKEND_ARGS:-}" ]]; then
    run_environment+=("SWC_SESSION_BACKEND_ARGS=${SWC_SESSION_BACKEND_ARGS}")
fi

if [[ "${window}" == "0" ]]; then
    echo "dev-run: client with no window, ${seconds} s at most, log ${client_log}" >&2
    set +e
    env "${run_environment[@]}" "${SWC_REPO}/tools/lib/session.sh" -- "${client_prefix[@]}" \
        timeout --kill-after=15 "${seconds}" "${SWC_CLIENT_CMD}" "${SWC_UPROJECT}" "${client_arguments[@]}" \
        -nullrhi -unattended -nosplash "$@" >"${logs}/${label}-console.log" 2>&1
    status=$?
    set -e
    echo "dev-run: status ${status} (console output: ${logs}/${label}-console.log)" >&2
    exit "${status}"
fi

# Each track of the project has its own agent desktop and data folder, so that their runs do not wait for each other
# (SWC_AGENT_DESKTOP, SWC_DEV_DATA; AGENTS.md).
desktop_id="${SWC_AGENT_DESKTOP:-soulwarden-client-dev}"
if [[ ! "${desktop_id}" =~ ^soulwarden-client-[a-z0-9-]+$ ]]; then
    echo "dev-run: SWC_AGENT_DESKTOP must start with soulwarden-client- (an own agent desktop of this project)." >&2
    exit 2
fi
game_size="${SWC_GAME_SIZE:-1256x660}"
if ! command -v gamescope >/dev/null 2>&1 || ! command -v omarchy-agent-desktop >/dev/null 2>&1; then
    echo "dev-run: gamescope or omarchy-agent-desktop was not found: no window run (the client never opens on the desktop of the user)." >&2
    exit 1
fi
if ! omarchy-agent-desktop status "${desktop_id}" >/dev/null 2>&1; then
    omarchy-agent-desktop create "Soulwarden client" --id "${desktop_id}" --mode "${SWC_DESKTOP_MODE:-1280x720@60}" >/dev/null
fi
# The world lock first (tools/lib/world-lock.sh): a session that waits for a level build or an import shows its window only
# after it, too late for the window wait of the agent desktop; such a window went to the workspace of the user (2026-10-19).
source "${SWC_REPO}/tools/lib/world-lock.sh"
if ! world_read true; then
    echo "dev-run: the world lock was not free in ${SWC_WORLD_RUN_WAIT:-900} s: no run (status 3)" >&2
    exit 3
fi
# The agent desktop starts the command with the environment of its compositor: the values go by name. gamescope runs
# outside the namespace (in a user namespace it refuses /tmp/.X11-unix, which the real root owns); the client in the
# namespace reaches its X server through the socket file.
omarchy-agent-desktop run --wait "${SWC_WINDOW_WAIT:-180}" --json "${desktop_id}" -- \
    env "${run_environment[@]}" \
    gamescope --backend wayland -W "${game_size%x*}" -H "${game_size#*x}" -w "${game_size%x*}" -h "${game_size#*x}" -r 60 -- \
    "${SWC_REPO}/tools/lib/session.sh" -- "${client_prefix[@]}" \
    "${SWC_CLIENT_EXE}" "${SWC_UPROJECT}" "${client_arguments[@]}" -windowed -ResX="${game_size%x*}" -ResY="${game_size#*x}" "$@" \
    >"${logs}/${label}-launch.json" 2>&1 || true
cat "${logs}/${label}-launch.json"
# The window guard for the life of the run (tools/lib/window_guard.py): a window of the launch that is not on the agent desktop is
# moved there (omarchy-agent-desktop adopt), else the run stops. Its log: LABEL-window-guard.log.
rm -f "${logs}/${label}-window-guard.log"
setsid nohup python3 -B "${SWC_REPO}/tools/lib/window_guard.py" "${desktop_id}" "${logs}/${label}-launch.json" \
    "${logs}/${label}-window-guard.log" >/dev/null 2>&1 < /dev/null &
