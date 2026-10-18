#!/usr/bin/env bash
# The render check of a stage of the materials track (unreal/SoulwardenClient/Scripts/material_proof.py): the proof
# meshes of tools/convert-materials.sh proof-plan STAGE in three variants, rendered offscreen to
# SWC_WORK/evidence/WF3/STAGE. It holds the import lock (it reads the assets that the imports write).
# Usage: tools/material-proof.sh STAGE [--wait N]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
export SWC_PROOF_STAGE="${1:?usage: tools/material-proof.sh STAGE [--wait N]}"
wait_lock=(-n)
[[ "${2:-}" == "--wait" ]] && wait_lock=(-w "${3:?--wait N}")
# The test folder of the proof (its level and transient instances) goes away first, under the lock: an editor cannot
# always delete a level package that a run before made.
flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    bash -c 'rm -rf "${SWC_GAME_CONTENT}/Content/Test/MaterialProof" && exec "$@"' _ \
    "${SWC_REPO}/tools/editor-script.sh" material_proof.py --render || status=$?
if [[ ${status:-0} -eq 75 ]]; then
    echo "material-proof: another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
# editor-script.sh ends with status 0; the Python lines are only in the log of the project. The proof is complete
# when its last line ("SWC PROOF") is there; the lines go next to the pictures.
evidence="${SWC_EVIDENCE:-${SWC_WORK}/evidence/WF3/${SWC_PROOF_STAGE}}"
grep -h "SWC " "${SWC_REPO}/unreal/SoulwardenClient/Saved/Logs/SoulwardenClient.log" | cut -c30- > "${evidence}/proof-${SWC_PROOF_STAGE}.log" || true
if ! grep -q "SWC PROOF" "${evidence}/proof-${SWC_PROOF_STAGE}.log"; then
    echo "material-proof: the script did not end (see ${SWC_WORK}/logs/material_proof.log)" >&2
    grep -h -E "LogPython: Error" "${SWC_WORK}/logs/material_proof.log" | head -5 >&2 || true
    status=1
fi
# A material that does not compile: Unreal draws its default material (the log has a warning, not an error).
# Only the lines after "parents ready": a parent of an older version can fail at its load, before it is made again.
if sed -n '/SWC parents ready/,$p' "${SWC_WORK}/logs/material_proof.log" | grep "Failed to compile Material" \
        | grep -o "/SwcGameContent/[A-Za-z0-9_/.]*" | sort -u | grep .; then
    echo "material-proof: a material does not compile" >&2
    status=1
fi
[[ ${status:-0} -eq 0 ]] && "${SWC_REPO}/tools/convert-materials.sh" proof-sheet "${SWC_PROOF_STAGE}"
exit "${status:-0}"
