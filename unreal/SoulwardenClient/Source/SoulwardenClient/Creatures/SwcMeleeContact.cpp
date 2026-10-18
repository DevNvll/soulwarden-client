#include "Creatures/SwcMeleeContact.h"

namespace swc::creatures {

std::uint64_t facet_target(std::uint64_t group, std::uint64_t localEntity, std::uint64_t component) {
    return (localEntity ^ group) + component;
}

swc::net::MeleeContactItem melee_contact_item(const swc::net::ActorId& actor, std::uint64_t facet, const swc::net::WorldPosition& point,
                                              const swc::net::WorldPosition& normal, const swc::net::WorldPosition& direction) {
    swc::net::MeleeContactItem item;
    item.provider = 0xFFFFFFFFU;
    item.groupActor = actor;
    item.group = swc::net::group_of(actor);
    item.actor = actor;
    item.entity = item.group;
    item.facet = facet;
    item.point = point;
    item.normal = normal;
    item.direction = direction;
    return item;
}

} // namespace swc::creatures
