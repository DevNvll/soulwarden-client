#!/usr/bin/env bash
# Builds and runs the tests of the environment (tests/environment) with the system compiler: the world time of the client
# (Net/Codec/SwcWorldTime.h) with the bytes of the encoders of the backend (server/rep/time_messages.cpp) and the values of its day cycle
# (world/world_time.cpp). The Soulwarden sources are read only; the build output goes to SWC_WORK/build/environment-tests.
# The backend file world_time.cpp also has the class WorldTime of the world: the build keeps only the functions that the test uses
# (function sections and the garbage collection of the linker).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
compiler="${CXX:-clang++}"
backend="${SOULWARDEN_REPO}/apps/backend"
module="${SWC_REPO}/unreal/SoulwardenClient/Source/SoulwardenClient"
vendor="${module}/Net/Vendor/soulwarden"
out="${SWC_WORK}/build/environment-tests"
mkdir -p "${out}"
flags=(-std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections
    -I"${module}" -I"${vendor}/src" -I"${vendor}/tests" -I"${backend}/src" -I"${backend}/tests")
nice -n 10 "${compiler}" "${flags[@]}" \
    "${SWC_REPO}/tests/environment/world_time_tests.cpp" \
    "${module}/Net/Codec/SwcRepMessages.cpp" "${module}/Net/Codec/SwcCarrier.cpp" "${vendor}/src/server/hub/wire.cpp" \
    "${backend}/src/server/rep/time_messages.cpp" "${backend}/src/world/world_time.cpp" \
    "${backend}/src/core/logging/log.cpp" "${backend}/src/middleware/compression/lz4.cpp" \
    -Wl,--gc-sections -o "${out}/swc-environment-tests"
exec "${out}/swc-environment-tests"
