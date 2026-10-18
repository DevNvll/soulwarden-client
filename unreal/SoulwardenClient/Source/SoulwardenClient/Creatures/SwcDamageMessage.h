#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Net/Codec/SwcRepMessages.h"

// The damage number of the attacker (protocol notes 4.15; the backend encoder server/rep/combat_messages.cpp): the message
// DamageReceiverComponentClientFacet_OnDamageDealt (type 2071) goes only to the client of the character that did the damage.
// Plain C++ with no engine header: tools/test-creatures.sh reads the bytes of the backend encoder.
//
//   target:u64 entity:u64            the damage receiver facet of the attacker (G + 0xCCC47F92DAF6BFC1) and its G
//   actor id:16 bytes G:u64          the entity that took the damage
//   u32 u32                          two keys (feedback, sound): 0 from the backend
//   flags:2 bytes                    low byte first: bit 0 ranged, bit 3 the damage was the death, bit 5 can crit,
//                                    bit 6 critical hit, bit 8 headshot
//   count:size, then for each entry: damage type:u8 amount:f32 part:f32   (part: 1 from the backend)

namespace swc::creatures {

struct DamageEntry {
    /** The damage type on the wire (slash 4, thrust 5, strike 6, ...), the damage, and the part before the defense. */
    std::uint8_t type{0};
    float amount{0.0F};
    float part{1.0F};
};

struct DamageMessage {
    std::uint64_t receiverTarget{0};
    std::uint64_t receiverEntity{0};
    swc::net::ActorId actor{};
    std::uint64_t group{0};
    std::uint32_t feedbackKey{0};
    std::uint32_t soundKey{0};
    std::uint16_t flags{0};
    std::vector<DamageEntry> entries;

    static constexpr std::uint16_t kRanged = 1U << 0;
    static constexpr std::uint16_t kCausedDeath = 1U << 3;
    static constexpr std::uint16_t kCanCrit = 1U << 5;
    static constexpr std::uint16_t kCrit = 1U << 6;
    static constexpr std::uint16_t kHeadshot = 1U << 8;

    /** @return The sum of the amounts of the entries. */
    [[nodiscard]] float total() const;
};

/** @return The message, or nothing when the body does not have the form (it must end after the last entry). */
std::optional<DamageMessage> read_damage_message(std::span<const std::uint8_t> body);

} // namespace swc::creatures
