# The gate of a run that renders (AGENTS.md, the run rules): a run slot, then the free GPU memory. The GPU is shared by
# the clients of all tracks and the retail game processes: a client that starts with too little free memory stops with
# "Failed to allocate Device Memory" (2026-10-19: 3 clients and a retail process on the 24 GB GPU). A memory check alone
# has a race (two runs see the same free memory and both start): the slots limit the client runs of this project that
# render to SWC_RUN_SLOTS (default 2). A slot is taken while any lock is on its file
# (SWC_WORK/unreal/run-slot-N.lock); a run holds a shared lock on it for its whole life (tools/lib/session.sh, from
# SWC_RUN_SLOT), and the lock ends with its process (tools/dev-stop.sh frees it). A live retail development run (a process
# with SOULWARDEN_DEV_RUN=1) counts as one slot: then only SWC_RUN_SLOTS - 1 clients of this project render next to it
# (2026-10-19: a client passed the memory check and stopped with a Vulkan out-of-memory fault at frame 1; a retail capture
# of about 4 GB ran, and the two other clients grew to 7 to 8 GB after their start). The programs of the user take no
# slot: the memory check covers them. Source it after tools/env.sh.

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

# retail_dev_run: the status is 0 when a retail development run of the Soulwarden project is alive: a process with
# SOULWARDEN_DEV_RUN=1 in its environment (/proc/PID/environ, read only; a process of this project never has it). With -q,
# grep gives 0 on a match also when it cannot read the environment of other users' processes (no pipe: pipefail).
retail_dev_run() {
    grep -qsazx 'SOULWARDEN_DEV_RUN=1' /proc/[0-9]*/environ 2>/dev/null
}

# run_slot_take LABEL: takes a free run slot for this shell (fd 6: a shared lock on run-slot-N.lock; a slot is free when an
# exclusive try succeeds) and exports SWC_RUN_SLOT=N. The slots in use are the held slots plus one for a live retail
# development run; a slot is taken only while they are fewer than SWC_RUN_SLOTS. The count and the take are in one
# exclusive hold of run-slot-gate.lock (fd 8, a short hold): two runs do not take the last two slots next to a retail run.
# Waits with one line each 30 s; the status is 3 when no slot is free within SWC_GPU_WAIT seconds (default 900).
run_slot_take() {
    local label="$1" slots="${SWC_RUN_SLOTS:-2}" limit="${SWC_GPU_WAIT:-900}" start=${SECONDS} next=0 n held=0 retail=0 free
    mkdir -p "${SWC_WORK}/unreal"
    while true; do
        exec 8>>"${SWC_WORK}/unreal/run-slot-gate.lock"
        if flock -w 30 -x 8; then
            held=0
            free=-1
            for ((n = 0; n < slots; ++n)); do
                exec 6>>"${SWC_WORK}/unreal/run-slot-${n}.lock"
                if flock -n -x 6; then
                    (( free < 0 )) && free=${n}
                    flock -u 6
                else
                    held=$((held + 1))
                fi
                exec 6>&-
            done
            retail=0
            retail_dev_run && retail=1
            if (( free >= 0 && held + retail < slots )); then
                exec 6>>"${SWC_WORK}/unreal/run-slot-${free}.lock"
                if flock -n -x 6 && flock -n -s 6; then
                    exec 8>&-
                    export SWC_RUN_SLOT="${free}"
                    echo "${label}: run slot ${free} of ${slots}$( (( retail )) && echo " (a retail development run takes one)")" >&2
                    return 0
                fi
                exec 6>&-
            fi
        fi
        exec 8>&-
        if (( SECONDS - start >= limit )); then
            echo "${label}: all ${slots} run slots are taken (${held} by clients, ${retail} by a retail development run) after ${limit} s: no run (status 3)" >&2
            return 3
        fi
        if (( SECONDS >= next )); then
            echo "${label}: all ${slots} run slots are taken (${held} by clients, ${retail} by a retail development run); the run waits (${limit} s at most)" >&2
            next=$((SECONDS + 30))
        fi
        sleep 3
    done
}

# run_slot_release: ends the hold of this shell (the session of the run holds its own lock on the slot).
run_slot_release() {
    exec 6>&-
}
