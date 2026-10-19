# The gate of a run that renders (AGENTS.md, the run rules): a run slot, then the free GPU memory. The GPU is shared by
# the clients of all tracks and the retail game processes: a client that starts with too little free memory stops with
# "Failed to allocate Device Memory" (2026-10-19: 3 clients and a retail process on the 24 GB GPU). A memory check alone
# has a race (two runs see the same free memory and both start): the slots limit the client runs of this project that
# render to SWC_RUN_SLOTS (default 2). A slot is taken while any lock is on its file
# (SWC_WORK/unreal/run-slot-N.lock); a run holds a shared lock on it for its whole life (tools/lib/session.sh, from
# SWC_RUN_SLOT), and the lock ends with its process (tools/dev-stop.sh frees it). The retail runs and the programs of
# the user take no slot: the memory check covers them. Source it after tools/env.sh.

# gpu_free_mib: prints the free memory of the GPU in MiB (nvidia-smi, read only), or nothing when it cannot be read.
gpu_free_mib() {
    command -v nvidia-smi >/dev/null 2>&1 || return 0
    nvidia-smi --query-gpu=memory.total,memory.used --format=csv,noheader,nounits 2>/dev/null \
        | awk -F', *' 'NR == 1 { print $1 - $2 }'
}

# gpu_wait LABEL: waits until the free GPU memory is at least SWC_GPU_FREE_MIB (default 9216), with one line each 30 s.
# The status is 3 when it is not free within SWC_GPU_WAIT seconds (default 900). No nvidia-smi: no wait.
gpu_wait() {
    local label="$1" need="${SWC_GPU_FREE_MIB:-9216}" limit="${SWC_GPU_WAIT:-900}" start=${SECONDS} free next=0
    while true; do
        free="$(gpu_free_mib)"
        [[ -z "${free}" ]] && return 0
        (( free >= need )) && return 0
        if (( SECONDS - start >= limit )); then
            echo "${label}: the free GPU memory is ${free} MiB, not ${need} MiB, after ${limit} s: no run (status 3)" >&2
            return 3
        fi
        if (( SECONDS >= next )); then
            echo "${label}: the free GPU memory is ${free} MiB; the run waits for ${need} MiB (${SWC_GPU_WAIT:-900} s at most)" >&2
            next=$((SECONDS + 30))
        fi
        sleep 5
    done
}

# run_slot_take LABEL: takes a free run slot for this shell (fd 6: a shared lock on run-slot-N.lock; a slot is free when an
# exclusive try succeeds) and exports SWC_RUN_SLOT=N. Waits with one line each 30 s; the status is 3 when no slot is free
# within SWC_GPU_WAIT seconds (default 900).
run_slot_take() {
    local label="$1" slots="${SWC_RUN_SLOTS:-2}" limit="${SWC_GPU_WAIT:-900}" start=${SECONDS} next=0 n
    mkdir -p "${SWC_WORK}/unreal"
    while true; do
        for ((n = 0; n < slots; ++n)); do
            exec 6>>"${SWC_WORK}/unreal/run-slot-${n}.lock"
            if flock -n -x 6 && flock -n -s 6; then
                export SWC_RUN_SLOT="${n}"
                echo "${label}: run slot ${n} of ${slots}" >&2
                return 0
            fi
            exec 6>&-
        done
        if (( SECONDS - start >= limit )); then
            echo "${label}: all ${slots} run slots are taken after ${limit} s: no run (status 3)" >&2
            return 3
        fi
        if (( SECONDS >= next )); then
            echo "${label}: all ${slots} run slots are taken; the run waits (${limit} s at most)" >&2
            next=$((SECONDS + 30))
        fi
        sleep 3
    done
}

# run_slot_release: ends the hold of this shell (the session of the run holds its own lock on the slot).
run_slot_release() {
    exec 6>&-
}
