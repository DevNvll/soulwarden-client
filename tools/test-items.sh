#!/usr/bin/env bash
# Builds and runs the tests of the bag and the equipment of the client (tests/net/item_tests.cpp) with the system C++ compiler:
# the states of the encoders of the backend (Container, Paperdoll, Currency, Progression: apps/backend/src/server/rep/states) into the read
# path of the client after the golden session of case T2, and the requests of the client (equip, unequip, swap, use, drop)
# against the readers of the backend (server/rep/item_requests.cpp). The Soulwarden sources are read only; the build output goes
# to SWC_WORK/build/item-tests.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
compiler="${CXX:-clang++}"
backend="${SOULWARDEN_REPO}/apps/backend"
module="${SWC_REPO}/unreal/SoulwardenClient/Source/SoulwardenClient"
vendor="${module}/Net/Vendor/soulwarden"
out="${SWC_WORK}/build/item-tests"
mkdir -p "${out}"
# The include order: the vendored copies first (the same files as the backend), then the backend sources.
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I"${module}" -I"${vendor}/src" -I"${vendor}/tests" -I"${backend}/src" -I"${backend}/tests" \
    "${SWC_REPO}/tests/net/item_tests.cpp" \
    "${module}/Net/Codec/SwcCarrier.cpp" "${module}/Net/Codec/SwcRepMessages.cpp" "${module}/Net/Codec/SwcRepClient.cpp" \
    "${module}/Interact/SwcInteraction.cpp" \
    "${vendor}/src/server/hub/wire.cpp" \
    "${backend}/src/server/carrier/carrier.cpp" "${backend}/src/server/rep/item_requests.cpp" \
    "${backend}/src/server/rep/replication.cpp" "${backend}/src/server/rep/states/container_state.cpp" \
    "${backend}/src/server/rep/states/paperdoll_state.cpp" "${backend}/src/server/rep/states/currency_state.cpp" \
    "${backend}/src/server/rep/states/progression_state.cpp" \
    "${backend}/src/world/item.cpp" \
    "${backend}/src/core/logging/log.cpp" "${backend}/src/middleware/compression/lz4.cpp" \
    -o "${out}/swc-item-tests"
exec "${out}/swc-item-tests" "${backend}/tests/golden/one_session.hex"
