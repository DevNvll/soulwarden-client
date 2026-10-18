#!/usr/bin/env bash
# Builds and runs the tests of the entity table of the creatures (tests/creatures) with the system C++ compiler. The tests
# use the states that the encoders of the backend make (metadata, movement, vitals, marker: apps/backend/src/server/rep/states).
# The Soulwarden sources are read only; the build output goes to SWC_WORK/build/creature-tests.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
compiler="${CXX:-clang++}"
backend="${SOULWARDEN_REPO}/apps/backend"
module="${SWC_REPO}/unreal/SoulwardenClient/Source/SoulwardenClient"
vendor="${module}/Net/Vendor/soulwarden"
out="${SWC_WORK}/build/creature-tests"
mkdir -p "${out}"
# The include order: the vendored copies first (the same files as the backend), then the backend sources.
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I"${module}" -I"${vendor}/src" -I"${vendor}/tests" -I"${backend}/src" -I"${backend}/tests" \
    "${SWC_REPO}/tests/creatures/entity_tests.cpp" \
    "${module}/Creatures/SwcEntityTable.cpp" "${module}/Net/Codec/SwcRepMessages.cpp" "${module}/Net/Codec/SwcCarrier.cpp" \
    "${vendor}/src/server/hub/wire.cpp" \
    "${backend}/src/server/rep/replication.cpp" "${backend}/src/server/rep/states/movement_state.cpp" \
    "${backend}/src/server/rep/states/vitals_state.cpp" "${backend}/src/server/rep/states/marker_state.cpp" \
    "${backend}/src/server/rep/states/metadata_state.cpp" \
    "${backend}/src/core/logging/log.cpp" "${backend}/src/middleware/compression/lz4.cpp" \
    -o "${out}/swc-creature-tests"
exec "${out}/swc-creature-tests"
