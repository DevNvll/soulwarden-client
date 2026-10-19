# The readers-writer lock of the world level (docs/WORLD.md, "The world lock"): SWC_WORK/unreal/world-build.lock.
# - A writer holds it exclusively: a call of the level build (tools/build-world.sh), an import that deletes or makes again
#   assets that the runs stream (tools/import.sh --world; tools/import-world.sh). A writer takes it before import.lock
#   (one order for all the tools: no deadlock).
# - A run of the client holds it shared from its start to its end (tools/lib/session.sh: tools/dev-run.sh and
#   tools/play.sh; tools/flythrough.sh). Many runs at a time; no run while a writer works.
# Source it after tools/env.sh.
SWC_WORLD_LOCK="${SWC_WORK}/unreal/world-build.lock"

# world_write WAIT COMMAND...: runs COMMAND with the exclusive lock. WAIT: -n (no wait) or -w SECONDS, the time for the
# runs to end. The status is 75 when the lock was not free in that time. flock -o: the editor does not get the descriptor
# of the lock (its trace and cache servers live after it).
world_write() {
    local wait=("$1")
    shift
    [[ "${wait[0]}" == "-w" ]] && { wait+=("$1"); shift; }
    mkdir -p "${SWC_WORK}/unreal"
    flock "${wait[@]}" -o -E 75 "${SWC_WORLD_LOCK}" "$@"
}

# world_read COMMAND...: runs COMMAND with the shared lock. It waits at most SWC_WORLD_RUN_WAIT seconds (default 900) for
# a writer to end; the status is 3 when the lock was not free in that time.
world_read() {
    mkdir -p "${SWC_WORK}/unreal"
    if ! flock -s -n "${SWC_WORLD_LOCK}" true; then
        echo "world lock: a level build or an import changes the world assets now: the run waits (${SWC_WORLD_RUN_WAIT:-900} s at most)" >&2
    fi
    flock -s -w "${SWC_WORLD_RUN_WAIT:-900}" -o -E 3 "${SWC_WORLD_LOCK}" "$@"
}

# world_read_fd FD: takes the shared lock on the open descriptor FD of SWC_WORLD_LOCK (a run that holds it in its own shell;
# the shell closes FD for the processes that can live after the run). The status is 3 when the lock was not free in time.
world_read_fd() {
    flock -s -n "$1" && return 0
    echo "world lock: a level build or an import changes the world assets now: the run waits (${SWC_WORLD_RUN_WAIT:-900} s at most)" >&2
    flock -s -w "${SWC_WORLD_RUN_WAIT:-900}" "$1" && return 0
    echo "world lock: the lock was not free in ${SWC_WORLD_RUN_WAIT:-900} s (${SWC_WORLD_LOCK}); the run was not started" >&2
    return 3
}
