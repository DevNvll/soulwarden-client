# The readers-writer lock of the world level (docs/WORLD.md, "The world lock"): SWC_WORK/unreal/world-build.lock.
# - A writer holds it exclusively: a call of the level build (tools/build-world.sh), an import that deletes or makes again
#   assets that the runs stream (tools/import.sh --world; tools/import-world.sh). A writer takes it before import.lock
#   (one order for all the tools: no deadlock).
# - A run of the client holds it shared from its start to its end (tools/lib/session.sh: tools/dev-run.sh and
#   tools/play.sh; tools/flythrough.sh). Many runs at a time; no run while a writer works.
# - A waiting writer goes first: flock gives a shared lock to a new run while a writer waits, so that a stream of runs
#   kept the writers waiting (2026-10-19: the mesh rebuild and a terrain import waited more than 25 min). A writer
#   writes a marker file (SWC_WORLD_WRITERS/ID, with the process id of its shell) while it waits and works; a new run
#   waits while a marker of a live process is there (a marker of a process that ended is removed).
# Source it after tools/env.sh.
SWC_WORLD_LOCK="${SWC_WORK}/unreal/world-build.lock"
SWC_WORLD_WRITERS="${SWC_WORK}/unreal/world-build.writers"

# world_write WAIT COMMAND...: runs COMMAND with the exclusive lock. WAIT: -n (no wait) or -w SECONDS, the time for the
# runs to end. The status is 75 when the lock was not free in that time. flock -o: the editor does not get the descriptor
# of the lock (its trace and cache servers live after it).
world_write() {
    local wait=("$1") marker status=0
    shift
    [[ "${wait[0]}" == "-w" ]] && { wait+=("$1"); shift; }
    mkdir -p "${SWC_WORK}/unreal" "${SWC_WORLD_WRITERS}"
    marker="${SWC_WORLD_WRITERS}/$$-${RANDOM}${RANDOM}"
    echo "$$" > "${marker}"
    flock "${wait[@]}" -o -E 75 "${SWC_WORLD_LOCK}" "$@" || status=$?
    rm -f "${marker}"
    return ${status}
}

# world_writer_waits: the status is 0 while a writer of a live process waits or works (its marker; /proc, not kill -0:
# a session in its own user namespace may not signal a process outside it).
world_writer_waits() {
    local marker pid
    for marker in "${SWC_WORLD_WRITERS}"/*; do
        [[ -f "${marker}" ]] || continue
        pid="$(cat "${marker}" 2>/dev/null)"
        if [[ -n "${pid}" && -d "/proc/${pid}" ]]; then
            return 0
        fi
        rm -f "${marker}"
    done
    return 1
}

# world_writers_first SECONDS: waits while a writer waits or works, at most SECONDS. Prints the seconds that are left.
world_writers_first() {
    local limit="$1" start=${SECONDS} told=0
    while world_writer_waits; do
        if (( SECONDS - start >= limit )); then
            break
        fi
        if (( told == 0 )); then
            echo "world lock: a level build or an import waits for the lock or works: the run waits (${limit} s at most)" >&2
            told=1
        fi
        sleep 2
    done
    echo $(( limit - (SECONDS - start) > 0 ? limit - (SECONDS - start) : 0 ))
}

# world_read COMMAND...: runs COMMAND with the shared lock. It waits at most SWC_WORLD_RUN_WAIT seconds (default 900) for
# the writers; the status is 3 when the lock was not free in that time.
world_read() {
    local left
    mkdir -p "${SWC_WORK}/unreal"
    left="$(world_writers_first "${SWC_WORLD_RUN_WAIT:-900}")"
    if ! flock -s -n "${SWC_WORLD_LOCK}" true; then
        echo "world lock: a level build or an import changes the world assets now: the run waits (${left} s at most)" >&2
    fi
    flock -s -w "${left}" -o -E 3 "${SWC_WORLD_LOCK}" "$@"
}

# world_read_fd FD: takes the shared lock on the open descriptor FD of SWC_WORLD_LOCK (a run that holds it in its own shell;
# the shell closes FD for the processes that can live after the run). The status is 3 when the lock was not free in time.
world_read_fd() {
    local left
    left="$(world_writers_first "${SWC_WORLD_RUN_WAIT:-900}")"
    flock -s -n "$1" && return 0
    echo "world lock: a level build or an import changes the world assets now: the run waits (${left} s at most)" >&2
    flock -s -w "${left}" "$1" && return 0
    echo "world lock: the lock was not free in ${SWC_WORLD_RUN_WAIT:-900} s (${SWC_WORLD_LOCK}); the run was not started" >&2
    return 3
}
