#!/usr/bin/env bash
# Builds and runs the tests of the water data of the client (tests/water; stage WF5, docs/WATER.md) with the system C++ compiler:
# the readers of the water files of the converter and the query of the water quadtree, against the reader of the converter
# (SWC_CONTENT/water/samples.json). Make the data first: tools/convert-water.sh write && tools/convert-water.sh samples.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
compiler="${CXX:-clang++}"
module="${SWC_REPO}/unreal/SoulwardenClient/Source/SoulwardenClient"
out="${SWC_WORK}/build/water-tests"
mkdir -p "${out}"
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra \
    -I"${module}" -I"${SOULWARDEN_REPO}/apps/backend/vendor" \
    "${SWC_REPO}/tests/water/water_tests.cpp" "${module}/Water/SwcWaterData.cpp" \
    -o "${out}/swc-water-tests"
exec "${out}/swc-water-tests" "${SWC_CONTENT}/water"
