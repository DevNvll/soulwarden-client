#!/usr/bin/env bash
# Builds and runs the tests of the network codec of the client (tests/net) with the system C++ compiler. The tests use
# bytes that the backend makes: the golden file of case T2 of the backend tests, the writers of its made-up client, its
# carrier, its input reader, and its messages of the life, the travel, and the interactions. Then the range rule of the
# interactions against the world of the backend (tests/net/interaction_range_tests.cpp; the core library of the backend:
# tools/lib/backend-core.sh), and the quest wire (tests/net/quest_tests.cpp). The Soulwarden sources are read only; the build output goes to SWC_WORK/build/net-tests.
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
    "${backend}/src/server/rep/progress_requests.cpp" \
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
"${out}/swc-interaction-range-tests"
# The town offers of the mission boards (C-12): the client generator (Boards/SwcTownOffers) against the backend generator with the real
# mission data (the board fixture of the backend tests; the Soulwarden sources read only) and the converted missiontown.json of SWC_CONTENT
# (tools/convert-missions.sh; with no such file the test is skipped).
if [[ -f "${SWC_CONTENT}/ui/gamedata/missiontown.json" ]]; then
    nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
        -I"${module}" -I"${backend}/src" -I"${backend}/tests" -I"${backend}/vendor" \
        "${SWC_REPO}/tests/net/board_offer_tests.cpp" "${module}/Boards/SwcTownOffers.cpp" \
        -L"${BACKEND_CORE}" -lsoulwarden_backend_core -lsoulwarden_sqlite -lsoulwarden_mbedtls -lpthread -ldl \
        -o "${out}/swc-board-offer-tests"
    "${out}/swc-board-offer-tests" "${SWC_GAMEDATA_DIR}/mission-data.json" "${SWC_CONTENT}/ui/gamedata/missiontown.json" | grep -v "^  ok "
else
    echo "the town offer tests: no ${SWC_CONTENT}/ui/gamedata/missiontown.json (tools/convert-missions.sh): skipped"
fi
# The level change in one connection (C-13; protocol notes 4.66): the messages of a backend of the test fixtures (ServerHost, the fake
# client of case IN4) to the RepClient, a story instance of the main world level and a level that the client does not have.
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I"${module}" -I"${backend}/src" -I"${backend}/tests" -I"${backend}/vendor" \
    "${SWC_REPO}/tests/net/level_change_tests.cpp" "${module}/Net/Codec/SwcRepMessages.cpp" "${module}/Net/Codec/SwcRepClient.cpp" \
    "${module}/Net/Codec/SwcCarrier.cpp" "${module}/Interact/SwcInteraction.cpp" "${module}/Interact/SwcConversation.cpp" \
    -L"${BACKEND_CORE}" -lsoulwarden_backend_core -lsoulwarden_sqlite -lsoulwarden_mbedtls -lpthread -ldl \
    -o "${out}/swc-level-change-tests"
"${out}/swc-level-change-tests" | grep -v "^  ok "
# The quest wire (Linear AST-236): the Objectives state of the backend encoder and the readers of the quest messages, with the quest data of
# the backend copy (tests/net/quest_tests.cpp).
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I"${module}" -I"${backend}/src" -I"${backend}/tests" -I"${backend}/vendor" \
    "${SWC_REPO}/tests/net/quest_tests.cpp" "${module}/Net/Codec/SwcRepMessages.cpp" "${module}/Net/Codec/SwcRepClient.cpp" \
    "${module}/Net/Codec/SwcCarrier.cpp" "${module}/Interact/SwcInteraction.cpp" "${module}/Interact/SwcConversation.cpp" \
    -L"${BACKEND_CORE}" -lsoulwarden_backend_core -lsoulwarden_sqlite -lsoulwarden_mbedtls -lpthread -ldl \
    -o "${out}/swc-quest-tests"
exec "${out}/swc-quest-tests" "${SWC_GAMEDATA_DIR}/quest-data.json"
