#!/usr/bin/env bash
# Writes the data of the object layer (stage P4.1 part 2; docs/OBJECTS.md) into SWC_CONTENT/world-objects (outside the repository):
# the interactable objects of the interaction data, the chests, the capital and natural gathering nodes, and the visuals of the
# node variants. See converter/swc_convert/objects.py.
# Usage: tools/convert-objects.sh write | meshes
#   write    the data files
#   meshes   convert the depleted meshes of the node variants that the manifest does not have (then tools/import.sh --keys
#            SWC_CONTENT/world-objects/depleted-meshes.json)
# The conversion runs with nice 10 and at most SWC_MEMORY_MAX (default 8G).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
exec systemd-run --user --scope -q -p MemoryMax="${SWC_MEMORY_MAX:-8G}" nice -n 10 python3 -B -m swc_convert.objects "$@"
