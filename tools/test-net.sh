#!/usr/bin/env bash
# Builds and runs the tests of the network codec of the client (tests/net) with the system C++ compiler. The tests use
# bytes that the backend makes: the golden file of case T2 of the backend tests, the writers of its made-up client, its
# carrier, its input reader, and its messages of the life, the travel, and the interactions. Then the range rule of the
# interactions against the world of the backend (tests/net/interaction_range_tests.cpp; the core library of the backend:
# tools/lib/backend-core.sh). The Soulwarden sources are read only; the build output goes to SWC_WORK/build/net-tests.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
compiler="${CXX:-clang++}"
backend="${SOULWARDEN_REPO}/apps/backend"
module="${SWC_REPO}/unreal/SoulwardenClient/Source/SoulwardenClient"
vendor="${module}/Net/Vendor/soulwarden"
out="${SWC_WORK}/build/net-tests"
mkdir -p "${out}"
# The include order: the vendored copies first (the same files as the backend), then the backend sources.
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I"${module}" -I"${vendor}/src" -I"${vendor}/tests" -I"${backend}/src" -I"${backend}/tests" \
    "${SWC_REPO}/tests/net/codec_tests.cpp" \
    "${module}/Net/Codec/SwcCarrier.cpp" "${module}/Net/Codec/SwcRepMessages.cpp" "${module}/Net/Codec/SwcRepClient.cpp" \
    "${module}/Interact/SwcInteraction.cpp" "${module}/Interact/SwcConversation.cpp" \
    "${vendor}/src/server/hub/wire.cpp" \
    "${backend}/src/server/carrier/carrier.cpp" "${backend}/src/server/rep/client_input.cpp" \
    "${backend}/src/server/rep/health_messages.cpp" "${backend}/src/server/rep/lock_messages.cpp" \
    "${backend}/src/server/rep/travel_messages.cpp" "${backend}/src/server/rep/item_requests.cpp" \
    "${backend}/src/server/rep/crafting_messages.cpp" \
    "${backend}/src/core/logging/log.cpp" "${backend}/src/middleware/compression/lz4.cpp" \
    -o "${out}/swc-net-tests"
"${out}/swc-net-tests" "${backend}/tests/golden/one_session.hex"
# The range rule with the backend headers and its core library only (no vendored copy: the backend has the same files).
source "${SWC_REPO}/tools/lib/backend-core.sh"
backend_core
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I"${module}" -I"${backend}/src" -I"${backend}/tests" -I"${backend}/vendor" \
    "${SWC_REPO}/tests/net/interaction_range_tests.cpp" "${module}/Interact/SwcInteraction.cpp" "${module}/Net/Codec/SwcRepMessages.cpp" \
    "${module}/Net/Codec/SwcCarrier.cpp" \
    -L"${BACKEND_CORE}" -lsoulwarden_backend_core -lsoulwarden_sqlite -lsoulwarden_mbedtls -lpthread -ldl \
    -o "${out}/swc-interaction-range-tests"
exec "${out}/swc-interaction-range-tests"
