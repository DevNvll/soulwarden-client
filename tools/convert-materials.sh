#!/usr/bin/env bash
# The rich material records of the materials track (docs/MATERIALS.md; converter/swc_convert/material_records.py),
# into SWC_CONTENT/materials (outside the repository).
# Usage: tools/convert-materials.sh audit | instances | record MTL [INDEX]
#   audit       the records of the source materials of the converted meshes, the overrides, and the far meshes; the
#               source slot of each mesh slot; the key collisions; the texture checks
#   instances   the spec of each material instance that changes and its textures (then tools/apply-materials.sh)
#   record      print the rich records of one material file
# The conversion runs with nice 10 and at most SWC_MEMORY_MAX (default 8G).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
exec systemd-run --user --scope -q -p MemoryMax="${SWC_MEMORY_MAX:-8G}" nice -n 10 python3 -B -m swc_convert.material_records "$@"
