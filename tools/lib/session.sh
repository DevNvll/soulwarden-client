#!/usr/bin/env bash
# Runs one backend copy and one client in one network namespace with loopback only (docs/NETWORK.md, the run model).
# Internal: tools/play.sh and tools/dev-run.sh start it. Usage: tools/lib/session.sh -- CLIENT COMMAND...
# Environment:
#   SWC_SESSION_DATA   the data folder of the backend (the database, the logs)
#   SWC_SESSION_LOCK   the lock file of that data folder: the session holds it (flock) while the backend runs
#   SWC_SESSION_SEED   a seed character for an account with no character (development runs), or empty
# The client runs as the user (the engine refuses to run as root), the backend as the root of the namespace.
# The status of the script is the status of the client: 3 when the lock is held (or the world lock is not free in
# SWC_WORLD_RUN_WAIT seconds), 4 when the backend did not start.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../env.sh"
# The mark of the Soulwarden development runs: never in a process of this project.
unset SOULWARDEN_DEV_RUN

if [[ "${SWC_SESSION_ISOLATED:-0}" != "1" ]]; then
    for tool in unshare ip flock curl; do
        if ! command -v "${tool}" >/dev/null 2>&1; then
            echo "session: ${tool} was not found. The run cannot start safely." >&2
            exit 1
        fi
    done
    SWC_SESSION_UID="$(id -u)"
    SWC_SESSION_GID="$(id -g)"
    export SWC_SESSION_UID SWC_SESSION_GID
    exec unshare --user --map-root-user --net bash -c '
        set -euo pipefail
        ip link set lo up
        export SWC_SESSION_ISOLATED=1
        exec "$@"
    ' bash "${BASH_SOURCE[0]}" "$@"
fi
[[ "${1:-}" == "--" ]] && shift
if [[ $# -eq 0 ]]; then
    echo "session: no client command" >&2
    exit 1
fi

# Safety: this namespace has the loopback interface and nothing more.
interfaces="$(ip -o link show | awk -F': ' '{print $2}' | tr '\n' ' ')"
if [[ "${interfaces}" != "lo " ]]; then
    echo "session: the network namespace has the interfaces '${interfaces}', not only 'lo'. The run was not started." >&2
    exit 1
fi

data="${SWC_SESSION_DATA:?SWC_SESSION_DATA is not set}"
mkdir -p "${data}/logs"
if [[ -n "${SWC_SESSION_LOCK:-}" ]]; then
    exec 9>"${SWC_SESSION_LOCK}"
    if ! flock -n 9; then
        echo "session: a different game process uses the data folder ${data} (${SWC_SESSION_LOCK} is held)." >&2
        exit 3
    fi
fi
# The world lock (docs/WORLD.md): shared for the whole run, so that no level build or import deletes or makes again an
# asset that the client streams. The backend and the client do not get the descriptor (fd 7).
source "$(dirname "${BASH_SOURCE[0]}")/world-lock.sh"
mkdir -p "${SWC_WORK}/unreal"
exec 7>>"${SWC_WORLD_LOCK}"
world_read_fd 7 || exit 3

health_url="http://127.0.0.1:${SWC_HTTP_PORT}/healthz"
backend=""
stop_backend() {
    if [[ -n "${backend}" ]] && kill -0 "${backend}" 2>/dev/null; then
        # The management route of the backend refuses a request with a service host: this request has none.
        curl --noproxy '*' --silent --max-time 3 --request POST "http://127.0.0.1:${SWC_HTTP_PORT}/v1/shutdown" >/dev/null 2>&1 || true
        for ((attempt = 0; attempt < 100; ++attempt)); do
            kill -0 "${backend}" 2>/dev/null || break
            sleep 0.1
        done
        kill "${backend}" 2>/dev/null || true
        for ((attempt = 0; attempt < 50; ++attempt)); do
            kill -0 "${backend}" 2>/dev/null || break
            sleep 0.1
        done
        kill -9 "${backend}" 2>/dev/null || true
    fi
    if [[ -n "${backend}" ]]; then
        wait "${backend}" 2>/dev/null || true
        echo "session: backend stopped" >&2
    fi
}
trap stop_backend EXIT
trap 'exit 130' INT TERM

backend_arguments=(--data-dir "${data}" --gamedata-dir "${SWC_GAMEDATA_DIR}" --http-host 127.0.0.1 --http-port "${SWC_HTTP_PORT}")
if [[ -n "${SWC_SESSION_SEED:-}" ]]; then
    backend_arguments+=(--seed-character "${SWC_SESSION_SEED}")
fi
# The backend gets no identity of the client and no diagnostics switch of the Soulwarden harness.
backend_environment=(-u SWC_STEAM_ID -u SWC_PERSONA_NAME -u SOULWARDEN_HARNESS -u SOULWARDEN_ADMIN_TOKEN)
# Development sessions only: SWC_SESSION_BACKEND_ARGS gives the backend extra arguments (for example --dev-commands, the
# development routes of the backend: tools/dev-build.sh), and its process alone gets the mark SOULWARDEN_HARNESS=1 that
# the backend needs for them. Never for the data folder of the user (a player run; tools/play.sh does not set it).
if [[ -n "${SWC_SESSION_BACKEND_ARGS:-}" ]]; then
    if [[ "$(realpath -m -- "${data}")" == "$(realpath -m -- "${SWC_PLAYER_DATA}")" ]]; then
        echo "session: SWC_SESSION_BACKEND_ARGS is for development sessions only, not for the data folder of the user." >&2
        exit 1
    fi
    read -r -a extra_arguments <<<"${SWC_SESSION_BACKEND_ARGS}"
    backend_arguments+=("${extra_arguments[@]}")
    backend_environment+=(SOULWARDEN_HARNESS=1)
    echo "session: development backend arguments: ${SWC_SESSION_BACKEND_ARGS}" >&2
fi
echo "session: backend $(basename "$(readlink -f "${SWC_BACKEND_DIR}")"), data ${data}" >&2
env "${backend_environment[@]}" \
    "${SWC_BACKEND_EXE}" "${backend_arguments[@]}" >>"${data}/logs/backend-launch.log" 2>&1 9>&- 7>&- &
backend=$!
ready=0
for ((attempt = 0; attempt < 300; ++attempt)); do
    if curl --noproxy '*' --silent --fail --max-time 1 "${health_url}" >/dev/null 2>&1; then
        ready=1
        break
    fi
    kill -0 "${backend}" 2>/dev/null || break
    sleep 0.1
done
if [[ "${ready}" != "1" ]]; then
    echo "session: the backend is not ready at ${health_url}" >&2
    tail -20 "${data}/logs/backend-launch.log" >&2 || true
    exit 4
fi
echo "session: backend ready, client starts" >&2

set +e
# No proxy: each request of the client goes to the backend on loopback.
unshare --user --map-user="${SWC_SESSION_UID}" --map-group="${SWC_SESSION_GID}" -- \
    env -u http_proxy -u https_proxy -u all_proxy -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY -u SWC_SESSION_BACKEND_ARGS "$@" 9>&- 7>&-
status=$?
set -e
echo "session: client ended with status ${status}" >&2
exit "${status}"
