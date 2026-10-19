#!/usr/bin/env bash
# Builds and runs the tests of the environment (tests/environment): the world time of the client (Net/Codec/SwcWorldTime.h) with the
# bytes of the encoders of the backend (server/rep/time_messages.cpp) and the values of its day cycle (world/world_time.cpp), with the
# system compiler; and the tests of the environment reader of the converter (test_environment.py: Python unittest, no game data).
# The Soulwarden sources are read only; the build output goes to SWC_WORK/build/environment-tests.
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
"${out}/swc-environment-tests"
# The rules of the runtime environment (stage E1): the evaluator against a table of the converter evaluator (the Everfall preset of
# SWC_CONTENT/environment, when it is there), the sun path against the samples of the research.
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -I"${module}" \
    "${SWC_REPO}/tests/environment/environment_rules_tests.cpp" "${module}/Environment/SwcEnvironmentRules.cpp" -o "${out}/swc-environment-rules-tests"
table="${out}/everfall-table.txt"
rm -f "${table}"
if [[ -f "${SWC_CONTENT}/environment/presets.json" ]]; then
    (cd "${SWC_REPO}/converter" && python3 -B -m swc_convert.environment table libs/timeofday/zones/everfall_a.xml "${table}" >/dev/null)
fi
"${out}/swc-environment-rules-tests" "${table}" "${HOME}/Games/soulwarden-client/research/world-environment/proof-metadata.json"
# The rules of the local lights (stage E2): the intensity against the source attenuation, the draw distance, the grid, the owner.
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -I"${module}" \
    "${SWC_REPO}/tests/environment/local_light_tests.cpp" "${module}/Environment/SwcLocalLightRules.cpp" -o "${out}/swc-local-light-tests"
"${out}/swc-local-light-tests"
cd "${SWC_REPO}"
exec python3 -B -m unittest discover -s tests/environment -p "test_*.py"
