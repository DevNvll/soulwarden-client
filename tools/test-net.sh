#!/usr/bin/env bash
# Builds and runs the tests of the network codec of the client (tests/net) with the system C++ compiler. The tests use
# bytes that the backend makes: the golden file of case T2 of the backend tests, the writers of its made-up client, its
# carrier, and its input reader. The Soulwarden sources are read only; the build output goes to SWC_WORK/build/net-tests.
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
    "${vendor}/src/server/hub/wire.cpp" \
    "${backend}/src/server/carrier/carrier.cpp" "${backend}/src/server/rep/client_input.cpp" \
    "${backend}/src/core/logging/log.cpp" "${backend}/src/middleware/compression/lz4.cpp" \
    -o "${out}/swc-net-tests"
exec "${out}/swc-net-tests" "${backend}/tests/golden/one_session.hex"
