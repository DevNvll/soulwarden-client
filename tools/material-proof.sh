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
flock "${wait_lock[@]}" -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    "${SWC_REPO}/tools/editor-script.sh" material_proof.py --render || status=$?
if [[ ${status:-0} -eq 75 ]]; then
    echo "material-proof: another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
[[ ${status:-0} -eq 0 ]] && "${SWC_REPO}/tools/convert-materials.sh" proof-sheet "${SWC_PROOF_STAGE}"
exit "${status:-0}"
