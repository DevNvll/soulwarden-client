#!/usr/bin/env bash
# The look preparation (AST-215, docs/CHARACTER.md "The prepared looks"): converts and imports all the entries of a wanted list
# (default SWC_WORK/player/wanted-all.json of tools/convert-player.sh all-looks) in chunks, as a background job.
# Usage: tools/prepare-looks.sh [--chunks N] [--only K] [--import-seconds S] [FILE]
#   --chunks N          the number of chunks (default 57: about 200 entries each); chunk K has the entries K, K + N, K + 2N, ...
#                       of the parts, the weapons, and the clip packs (each chunk is a mixed sample)
#   --only K            only chunk K (the measurement batch: --only 0)
#   --import-seconds S  the time of one import call (default 540: the import lock is free between the calls)
# The converter of chunk K + 1 runs while chunk K imports (the manifest is written in one step). Each import call waits for
# 14 GB of available RAM (the rule of the project). The logs: SWC_WORK/logs/prepare-looks/ (one line for each step in
# progress.log, the converter and import reports of each chunk); failures.json: the entries that did not convert or import.
# At the end, one more pass converts and imports the failures.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
chunks=57 only="" seconds=540 file="${SWC_WORK}/player/wanted-all.json"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --chunks) chunks="$2"; shift 2 ;;
        --only) only="$2"; shift 2 ;;
        --import-seconds) seconds="$2"; shift 2 ;;
        *) file="$1"; shift ;;
    esac
done
logs="${SWC_WORK}/logs/prepare-looks"
mkdir -p "${logs}/chunks"
log() { echo "$(date +%T) $*" >> "${logs}/progress.log"; echo "$(date +%T) $*"; }

# The chunk files: an interleaved split of each list.
python3 -B - "${file}" "${chunks}" "${logs}/chunks" <<'EOF'
import json, sys
request, count, folder = json.load(open(sys.argv[1])), int(sys.argv[2]), sys.argv[3]
for k in range(count):
    part = {name: request.get(name, [])[k::count] for name in ("parts", "weapons", "clips")}
    json.dump(part, open(f"{folder}/chunk-{k:03d}.json", "w"))
EOF

convert() {   # convert FILE LABEL: the converter in its own memory scope; its report in LABEL-convert.json
    local started=${SECONDS}
    systemd-run --user --scope -q --unit="swc-prepare-looks-$2-$(date +%s%N)" -p MemoryMax=8G \
        "${SWC_REPO}/tools/convert-player.sh" wanted "$1" > "${logs}/$2-convert.json" 2> "${logs}/$2-convert.err" || true
    log "$2: converted in $((SECONDS - started)) s: $(python3 -B -c "import json; r=json.load(open('${logs}/$2-convert.json')); print(r['converted'], 'done,', r['failed'], 'failed')" 2>/dev/null || echo 'no report')"
}

import_all() {   # import_all LABEL: import calls of at most SECONDS until nothing of the manifest is pending
    local call=0 started=${SECONDS}
    while :; do
        call=$((call + 1))
        while (( $(free -g | awk '/Mem/{print $7}') < 14 )); do sleep 30; done
        SWC_IMPORT_MAX_SECONDS="${seconds}" "${SWC_REPO}/tools/import-player.sh" > "${logs}/$1-import-${call}.out" 2>&1 || true
        cp "${SWC_CONTENT}/player/import-report.json" "${logs}/$1-import-${call}.json" 2>/dev/null || true
        cp "${SWC_WORK}/logs/import-player.log" "${logs}/$1-import-${call}.log" 2>/dev/null || true
        local state
        state=$(python3 -B -c "import json; r=json.load(open('${logs}/$1-import-${call}.json')); print(r['imported'], r['failed'], r.get('incomplete'), r.get('seconds'))" 2>/dev/null || echo "none")
        log "$1: import call ${call}: imported, failed, incomplete, seconds: ${state}"
        [[ "${state}" == *" True "* ]] || break
    done
    log "$1: imported in $((SECONDS - started)) s, ${call} call(s)"
}

failures() {   # the failed entries of all the reports, as a wanted list
    python3 -B - "${logs}" "${file}" <<'EOF'
import glob, json, sys
logs, request = sys.argv[1], json.load(open(sys.argv[2]))
convert, imported = {}, []
for path in glob.glob(f"{logs}/*-convert.json"):
    try:
        convert.update(json.load(open(path))["failures"])
    except Exception:
        pass
for path in sorted(glob.glob(f"{logs}/*-import-*.json")):
    try:
        imported = json.load(open(path))["failures"]   # the last report has the current failures
    except Exception:
        pass
retry = {"parts": [p for p in request["parts"] if "|".join(p) in convert],
         "weapons": [w for w in request["weapons"] if w in convert], "clips": [c for c in request["clips"] if f"clips|{c[0]}|{c[1]}" in convert]}
json.dump({"convert": convert, "import": imported}, open(f"{logs}/failures.json", "w"), indent=1)
json.dump(retry, open(f"{logs}/retry.json", "w"))
print(len(convert), "convert failures,", len(imported), "import failures")
EOF
}

started=${SECONDS}
if [[ -n "${only}" ]]; then
    label=$(printf 'chunk-%03d' "${only}")
    log "${label}: start (one chunk)"
    convert "${logs}/chunks/${label}.json" "${label}"
    import_all "${label}"
    touch "${logs}/${label}-imported"
else
    log "start: ${chunks} chunks of ${file}"
    [[ -f "${logs}/chunk-000-convert.json" ]] || convert "${logs}/chunks/chunk-000.json" "chunk-000"
    for ((k = 0; k < chunks; k++)); do
        label=$(printf 'chunk-%03d' "${k}")
        next=$(printf 'chunk-%03d' $((k + 1)))
        converter=""
        if (( k + 1 < chunks )) && [[ ! -f "${logs}/${next}-convert.json" ]]; then
            convert "${logs}/chunks/${next}.json" "${next}" &
            converter=$!
        fi
        [[ -f "${logs}/${label}-imported" ]] || { import_all "${label}"; touch "${logs}/${label}-imported"; }
        [[ -n "${converter}" ]] && wait "${converter}"
    done
    log "retry: $(failures)"
    convert "${logs}/retry.json" "retry"
    import_all "retry"
fi
log "end after $((SECONDS - started)) s: $(failures)"
