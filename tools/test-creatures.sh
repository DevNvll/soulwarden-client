#!/usr/bin/env bash
# Builds and runs the tests of the creatures (tests/creatures) with the system C++ compiler: the entity table with the states
# that the encoders of the backend make (metadata, movement, vitals, marker: apps/backend/src/server/rep/states), the melee
# contact item with the reader of the backend (server/rep/client_input.cpp) and its test client (tests/support/client_frames.h),
# the damage number with the encoder of the backend (server/rep/combat_messages.cpp), and the shots and the danger areas with the
# encoders of the Projectile, Spell, PositionInTheWorld, and DelayedEvent states, and the loot bag (server/rep/states), and the
# objects of the level data (stage P4.1 part 2: the id rules, the GatherableController and Achievement states, the node file).
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
flags=(-std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter
    -I"${module}" -I"${vendor}/src" -I"${vendor}/tests" -I"${backend}/src" -I"${backend}/tests")
nice -n 10 "${compiler}" "${flags[@]}" \
    "${SWC_REPO}/tests/creatures/entity_tests.cpp" \
    "${module}/Creatures/SwcEntityTable.cpp" "${module}/Net/Codec/SwcRepMessages.cpp" "${module}/Net/Codec/SwcCarrier.cpp" \
    "${vendor}/src/server/hub/wire.cpp" \
    "${backend}/src/server/rep/replication.cpp" "${backend}/src/server/rep/states/movement_state.cpp" \
    "${backend}/src/server/rep/states/vitals_state.cpp" "${backend}/src/server/rep/states/marker_state.cpp" \
    "${backend}/src/server/rep/states/metadata_state.cpp" "${backend}/src/server/rep/states/status_state.cpp" \
    "${backend}/src/core/logging/log.cpp" "${backend}/src/middleware/compression/lz4.cpp" \
    -o "${out}/swc-creature-tests"
nice -n 10 "${compiler}" "${flags[@]}" \
    "${SWC_REPO}/tests/creatures/combat_tests.cpp" "${module}/Creatures/SwcMeleeContact.cpp" "${module}/Creatures/SwcDamageMessage.cpp" \
    "${module}/Net/Codec/SwcRepMessages.cpp" "${module}/Net/Codec/SwcCarrier.cpp" "${vendor}/src/server/hub/wire.cpp" \
    "${backend}/src/server/rep/client_input.cpp" "${backend}/src/server/rep/combat_messages.cpp" "${backend}/src/core/logging/log.cpp" \
    -o "${out}/swc-combat-tests"
nice -n 10 "${compiler}" "${flags[@]}" \
    "${SWC_REPO}/tests/creatures/effect_tests.cpp" "${module}/Creatures/SwcEntityTable.cpp" "${module}/Creatures/SwcEffects.cpp" \
    "${module}/Creatures/SwcMeleeContact.cpp" "${backend}/src/server/rep/states/interaction_states.cpp" \
    "${backend}/src/server/rep/states/loot_bag_states.cpp" \
    "${module}/Net/Codec/SwcRepMessages.cpp" "${module}/Net/Codec/SwcCarrier.cpp" "${vendor}/src/server/hub/wire.cpp" \
    "${backend}/src/server/rep/replication.cpp" "${backend}/src/server/rep/states/metadata_state.cpp" \
    "${backend}/src/server/rep/states/projectile_state.cpp" "${backend}/src/server/rep/states/spell_state.cpp" \
    "${backend}/src/server/rep/states/object_states.cpp" "${backend}/src/world/ids.cpp" \
    "${backend}/src/core/logging/log.cpp" "${backend}/src/middleware/compression/lz4.cpp" \
    -o "${out}/swc-effect-tests"
nice -n 10 "${compiler}" "${flags[@]}" \
    "${SWC_REPO}/tests/creatures/object_tests.cpp" "${module}/Objects/SwcObjectRules.cpp" "${module}/Creatures/SwcEntityTable.cpp" \
    "${module}/Creatures/SwcEffects.cpp" "${module}/Creatures/SwcMeleeContact.cpp" \
    "${module}/Net/Codec/SwcRepMessages.cpp" "${module}/Net/Codec/SwcCarrier.cpp" "${vendor}/src/server/hub/wire.cpp" \
    "${backend}/src/server/rep/replication.cpp" "${backend}/src/server/rep/states/metadata_state.cpp" \
    "${backend}/src/server/rep/states/interaction_states.cpp" "${backend}/src/server/rep/states/gatherable_state.cpp" \
    "${backend}/src/server/rep/states/ownership_states.cpp" "${backend}/src/world/ids.cpp" \
    "${backend}/src/core/logging/log.cpp" "${backend}/src/middleware/compression/lz4.cpp" \
    -o "${out}/swc-object-tests"
"${out}/swc-creature-tests"
"${out}/swc-combat-tests"
"${out}/swc-effect-tests"
exec "${out}/swc-object-tests"
