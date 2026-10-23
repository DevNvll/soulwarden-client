#!/usr/bin/env bash
# The tests of the scene key curves (stage Q5, AST-240; tests/scenes/curve_tests.cpp: the retail rule of research/scene-curves.md, the worked
# beach camera pair). No engine. The build output goes to SWC_WORK/build/scene-tests.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
out="${SWC_WORK}/build/scene-tests"
mkdir -p "${out}"
nice -n 10 "${CXX:-g++}" -std=c++20 -O1 -Wall -Wextra -I"${SWC_REPO}/unreal/SoulwardenClient/Source/SoulwardenClient" \
    "${SWC_REPO}/tests/scenes/curve_tests.cpp" -o "${out}/swc-scene-tests"
exec "${out}/swc-scene-tests"
