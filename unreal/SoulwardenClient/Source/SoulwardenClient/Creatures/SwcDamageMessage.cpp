#include "Creatures/SwcDamageMessage.h"

namespace swc::creatures {

float DamageMessage::total() const {
    float sum = 0.0F;
    for (const DamageEntry& entry : entries) {
        sum += entry.amount;
    }
    return sum;
}

std::optional<DamageMessage> read_damage_message(std::span<const std::uint8_t> body) {
    swc::net::Reader in(body);
    DamageMessage out;
    out.receiverTarget = in.u64();
    out.receiverEntity = in.u64();
    for (std::uint8_t& byte : out.actor) {
        byte = in.u8();
    }
    out.group = in.u64();
    out.feedbackKey = in.u32();
    out.soundKey = in.u32();
    // The flags are two raw bytes, the low byte first.
    const std::uint8_t low = in.u8();
    const std::uint8_t high = in.u8();
    out.flags = static_cast<std::uint16_t>(low | (high << 8));
    const std::uint32_t count = in.size();
    // An entry has 9 bytes: a count above the rest of the body is no message.
    if (!in.ok() || count > in.left() / 9) {
        return std::nullopt;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        DamageEntry entry;
        entry.type = in.u8();
        entry.amount = in.f32();
        entry.part = in.f32();
        out.entries.push_back(entry);
    }
    if (!in.ok() || in.left() != 0) {
        return std::nullopt;
    }
    return out;
}

} // namespace swc::creatures
