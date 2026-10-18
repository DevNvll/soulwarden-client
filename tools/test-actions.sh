#!/usr/bin/env bash
# Builds and runs the tests of the attack graph of the client (tests/actions; stage P2.7) with the system C++ compiler: the
# action order and the marker order with the converted data (SWC_CONTENT/player/actions.json), and the parity with the
# attack sequence of the backend (its sources, read only) on the same input steps. The backend reads the graph file of the
# backend copy (SWC_GAMEDATA_DIR/weapon-actions.json). The build output goes to SWC_WORK/build/actions-tests.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
compiler="${CXX:-clang++}"
backend="${SOULWARDEN_REPO}/apps/backend"
module="${SWC_REPO}/unreal/SoulwardenClient/Source/SoulwardenClient"
gamedata="${SWC_GAMEDATA_DIR}"
out="${SWC_WORK}/build/actions-tests"
mkdir -p "${out}"
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I"${module}" -I"${backend}/src" -I"${backend}/vendor" \
    "${SWC_REPO}/tests/actions/actions_tests.cpp" "${module}/Combat/SwcAttackGraph.cpp" "${module}/Combat/SwcStamina.cpp" \
    "${backend}/src/world/attack_sequence.cpp" "${backend}/src/gamedata/weapon_actions.cpp" \
    -o "${out}/swc-actions-tests"
exec "${out}/swc-actions-tests" "${SWC_CONTENT}/player/actions.json" "${gamedata}/weapon-actions.json"
