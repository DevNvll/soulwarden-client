#!/usr/bin/env bash
# Builds and runs the tests of the movement of the client (stage P3.3) with the system C++ compiler:
# - tests/movement: the speeds of each case against the motion lists of the data (SWC_CONTENT/player/movement.json,
#   tools/convert-player.sh movement) and the values of the research, the posture keys, autorun, the limits of the load and of
#   the statuses, and the position check of the backend (world/position_report.cpp) with the reports of a simulated client at
#   these speeds.
# - tests/net/own_state_tests.cpp: the speed table (slot 43), the status map (slot 44), and the owner states of stage MVP-A (the
#   attributes, the ranks, the point pools, the hit counts, the rune charge, the craft state, the entitlements) of the encoders of
#   the backend (server/rep/states) into the read path of the client after the golden session of case T2.
# The Soulwarden sources are read only; the build output goes to SWC_WORK/build/movement-tests.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
compiler="${CXX:-clang++}"
backend="${SOULWARDEN_REPO}/apps/backend"
module="${SWC_REPO}/unreal/SoulwardenClient/Source/SoulwardenClient"
vendor="${module}/Net/Vendor/soulwarden"
out="${SWC_WORK}/build/movement-tests"
mkdir -p "${out}"
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I"${module}" -I"${backend}/src" -I"${backend}/vendor" \
    "${SWC_REPO}/tests/movement/movement_tests.cpp" "${module}/Character/SwcMoveRules.cpp" \
    "${backend}/src/world/position_report.cpp" \
    -o "${out}/swc-movement-tests"
# The include order: the vendored copies first (the same files as the backend), then the backend sources.
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I"${module}" -I"${vendor}/src" -I"${vendor}/tests" -I"${backend}/src" -I"${backend}/tests" \
    "${SWC_REPO}/tests/net/own_state_tests.cpp" \
    "${module}/Net/Codec/SwcCarrier.cpp" "${module}/Net/Codec/SwcRepMessages.cpp" "${module}/Net/Codec/SwcRepClient.cpp" \
    "${module}/Interact/SwcInteraction.cpp" \
    "${vendor}/src/server/hub/wire.cpp" \
    "${backend}/src/server/rep/replication.cpp" "${backend}/src/server/rep/states/stat_multiplier_state.cpp" \
    "${backend}/src/server/rep/states/status_state.cpp" "${backend}/src/server/rep/states/attribute_state.cpp" \
    "${backend}/src/server/rep/states/skill_states.cpp" "${backend}/src/server/rep/states/charge_state.cpp" \
    "${backend}/src/server/rep/states/crafting_state.cpp" "${backend}/src/server/rep/states/ownership_states.cpp" \
    "${backend}/src/core/logging/log.cpp" "${backend}/src/middleware/compression/lz4.cpp" \
    -o "${out}/swc-own-state-tests"
"${out}/swc-movement-tests" "${SWC_CONTENT}/player/movement.json"
exec "${out}/swc-own-state-tests" "${backend}/tests/golden/one_session.hex"
