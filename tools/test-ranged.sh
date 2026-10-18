#!/usr/bin/env bash
# Builds and runs the tests of the ranged and magic actions of the client (tests/ranged; stage P3.6) with the system C++ compiler:
# the player of the action lists and the animation databases of the data (Combat/SwcRanged) with the programs of actions.json
# (tools/convert-player.sh actions), against the step rules of the backend and the live values of the Soulwarden protocol notes; and
# the mana state, the prediction sync 4289, the heal numbers of the owner vitals, and message 4298 of the encoders of the backend into
# the read path of the client; and the parity of the presses: the input entries of the attack driver into the world of the backend
# (its core library) with the projectile tables of the backend copy. The Soulwarden sources are read only; the build output goes to
# SWC_WORK/build/ranged-tests and SWC_WORK/build/backend-core.
# Usage: tools/test-ranged.sh [--trace]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
compiler="${CXX:-clang++}"
backend="${SOULWARDEN_REPO}/apps/backend"
module="${SWC_REPO}/unreal/SoulwardenClient/Source/SoulwardenClient"
out="${SWC_WORK}/build/ranged-tests"
mkdir -p "${out}"
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I"${module}" -I"${backend}/vendor" \
    "${SWC_REPO}/tests/ranged/ranged_tests.cpp" "${module}/Combat/SwcRanged.cpp" \
    -o "${out}/swc-ranged-tests"
vendor="${module}/Net/Vendor/soulwarden"
# The mana and the heal numbers with bytes of the backend encoders (tests/net/ranged_state_tests.cpp). The include order: the vendored
# copies first (the same files as the backend), then the backend sources.
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I"${module}" -I"${vendor}/src" -I"${vendor}/tests" -I"${backend}/src" -I"${backend}/tests" \
    "${SWC_REPO}/tests/net/ranged_state_tests.cpp" \
    "${module}/Net/Codec/SwcCarrier.cpp" "${module}/Net/Codec/SwcRepMessages.cpp" "${module}/Net/Codec/SwcRepClient.cpp" \
    "${module}/Interact/SwcInteraction.cpp" \
    "${vendor}/src/server/hub/wire.cpp" \
    "${backend}/src/server/rep/replication.cpp" "${backend}/src/server/rep/states/vitals_state.cpp" \
    "${backend}/src/server/rep/states/mana_state.cpp" "${backend}/src/server/rep/mana_messages.cpp" \
    "${backend}/src/server/rep/health_messages.cpp" "${backend}/src/server/rep/item_requests.cpp" \
    "${backend}/src/server/rep/travel_messages.cpp" \
    "${backend}/src/core/logging/log.cpp" "${backend}/src/middleware/compression/lz4.cpp" \
    -o "${out}/swc-ranged-state-tests"
# The parity of the presses (tests/ranged/parity_tests.cpp): the input entries of the attack driver into the world of the backend. The
# core library of the backend builds once outside the Soulwarden repository (its root CMake file with no game hook; 4 jobs).
core="${SWC_WORK}/build/backend-core"
if [[ ! -f "${core}/apps/backend/libsoulwarden_backend_core.a" || -n "${SWC_REBUILD_BACKEND_CORE:-}" ]]; then
    mkdir -p "${core}"
    nice -n 10 cmake -S "${SOULWARDEN_REPO}" -B "${core}" -G Ninja -DSOULWARDEN_BUILD_GAME_HOOK=OFF -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang > "${core}/configure.log"
    systemd-run --user --scope -q -p MemoryMax=8G nice -n 10 ninja -j "${SWC_BUILD_JOBS:-4}" -C "${core}" soulwarden_backend_core > "${core}/build.log"
fi
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I"${module}" -I"${backend}/src" -I"${backend}/tests" -I"${backend}/vendor" \
    "${SWC_REPO}/tests/ranged/parity_tests.cpp" "${module}/Combat/SwcAttackGraph.cpp" "${module}/Combat/SwcStamina.cpp" \
    "${module}/Combat/SwcRanged.cpp" \
    -L"${core}/apps/backend" -lsoulwarden_backend_core -lsoulwarden_sqlite -lsoulwarden_mbedtls -lpthread -ldl \
    -o "${out}/swc-ranged-parity-tests"
"${out}/swc-ranged-tests" "${SWC_CONTENT}/player/actions.json" "$@"
"${out}/swc-ranged-state-tests" "${backend}/tests/golden/one_session.hex"
exec "${out}/swc-ranged-parity-tests" "${SWC_CONTENT}/player/actions.json" "${SWC_GAMEDATA_DIR}/projectile-data.json"
