#!/usr/bin/env bash
# The particle effects (AST-210, docs/PARTICLES.md; converter/swc_convert/particles.py and particle_systems.py) into
# SWC_CONTENT/particles (outside the repository): the libraries, the placements of the world and of the menu, the name checks,
# the support report, the systems for the importer and their textures, and the checks of the worked examples.
# Usage: tools/convert-particles.sh library|placements [REGION...]|menu|names|report|systems [KEY...]|check|all
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${SWC_REPO}/converter"
exec systemd-run --user --scope -q -p MemoryMax=8G nice -n 10 python3 -B -m swc_convert.particles "$@"
