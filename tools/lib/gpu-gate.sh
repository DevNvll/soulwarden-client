# The GPU memory gate of a run that renders (AGENTS.md, the run rules). The GPU is shared by the clients of all tracks
# and the retail game processes: a client that starts with too little free memory stops with "Failed to allocate Device
# Memory" (2026-10-19: 3 clients and a retail process on the 24 GB GPU). Source it after tools/env.sh.

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
