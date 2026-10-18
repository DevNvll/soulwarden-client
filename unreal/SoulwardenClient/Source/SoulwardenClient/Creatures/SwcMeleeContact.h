#pragma once

#include <cstdint>

#include "Net/Codec/SwcRepMessages.h"

// The melee contact of a creature (protocol notes 4.7 and 4.15; backend server/rep/client_input.h, InputContact): the creature
// side of the item of list 3 of an input entry. The item and its writer are the ones of the input codec
// (swc::net::MeleeContactItem, swc::net::write_melee_contact). Plain C++ with no engine header: tools/test-creatures.sh reads
// the bytes with the reader of the backend.
//
// A creature is one entity: the group and the receiver of the item are its actor id and its G. The facet target is the target
// of its hit volume component by the id rule of protocol notes 4.2: (entity id XOR root id XOR G) + component id.

namespace swc::creatures {

/**
 * @return The facet target of a component of a slice for an entity group id G: (local id XOR G) + component id, with the
 *         local id = the entity id XOR the root id of the slice (slices.json). The sum wraps at 64 bits.
 */
std::uint64_t facet_target(std::uint64_t group, std::uint64_t localEntity, std::uint64_t component);

/** @return The item of a contact with a creature: provider 0xFFFFFFFF, the actor id and G of the creature two times, the facet
 *  target of the hit volume, and the point, the normal, and the direction in engine space. */
swc::net::MeleeContactItem melee_contact_item(const swc::net::ActorId& actor, std::uint64_t facet, const swc::net::WorldPosition& point,
                                              const swc::net::WorldPosition& normal, const swc::net::WorldPosition& direction);

} // namespace swc::creatures
