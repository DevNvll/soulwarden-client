#include "Net/Codec/SwcRepMessages.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace swc::net {
namespace {

constexpr std::uint8_t kEnvelopeTarget = 0x01;
constexpr std::uint8_t kEnvelopeSource = 0x02;
constexpr std::uint8_t kEnvelopeExtension = 0x04;

/** The text of the 6 entries of the version map of the registration request (the made-up client of the backend tests). */
constexpr const char* kClientVersion = "1.400.6031.6004151";

/** The height of a world position on the wire: 16 bits of the range -100 m to 1000 m. */
constexpr float kHeightMin = -100.0F;
constexpr float kHeightRange = 1100.0F;

std::string hex_text(std::string_view bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes) {
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}

bool read_actor(Reader& reader, ActorId& actor) {
    const auto bytes = reader.raw(actor.size());
    if (!reader.ok()) {
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), actor.begin());
    return true;
}

/** identity := flags:u8, then a 16-byte id when bit 0 is set, else a text. */
bool read_identity(Reader& reader, ActorId* actor) {
    const std::uint8_t flags = reader.u8();
    if ((flags & 1) != 0) {
        ActorId id{};
        if (!read_actor(reader, id)) {
            return false;
        }
        if (actor != nullptr) {
            *actor = id;
        }
    } else {
        reader.string();
    }
    return reader.ok();
}

bool read_reference(Reader& reader, ActorReference& reference) {
    reference.hub = reader.u32();
    return read_actor(reader, reference.actor) && read_actor(reader, reference.type);
}

void write_position(Writer& out, float x, float y, std::uint16_t height) {
    out.f32(x);
    out.f32(y);
    out.u16(height);
}

/** The height of the camera: rounded (the made-up client of the backend tests). */
std::uint16_t camera_height(float z) {
    const float clamped = std::min(std::max(z, kHeightMin), kHeightMin + kHeightRange);
    return static_cast<std::uint16_t>(std::lround((clamped - kHeightMin) / kHeightRange * 65535.0F));
}

/** The height of the character: cut off, as the retail client does (protocol notes 4.7). */
std::uint16_t character_height(float z) {
    const float clamped = std::min(std::max(z, kHeightMin), kHeightMin + kHeightRange);
    return static_cast<std::uint16_t>((clamped - kHeightMin) / kHeightRange * 65535.0F);
}

void write_vector(Writer& out, const WorldPosition& value) {
    out.f32(value.x);
    out.f32(value.y);
    out.f32(value.z);
}

void write_input_entry(Writer& w, const InputEntry& entry, const InputEntry* before) {
    std::uint32_t mask = 0;
    Writer fields;
    if (entry.flags != (before != nullptr ? before->flags : 0)) {
        mask |= 0x01;
        fields.size64(entry.flags);
    }
    if (entry.permissions >= 0) {
        mask |= 0x02;
        fields.u8(static_cast<std::uint8_t>(entry.permissions));
    }
    if (entry.depthClass > 0) {
        mask |= 0x08;
        fields.u8(static_cast<std::uint8_t>(entry.depthClass));
    }
    if (entry.groundDistance >= 0) {
        mask |= 0x10;
        fields.u16(static_cast<std::uint16_t>(entry.groundDistance));
    }
    // The action words: in pairs, and one word alone when the count is odd.
    std::vector<std::uint32_t> words(entry.actionWords, 0);
    for (const unsigned bit : entry.actionBits) {
        if (bit / 32 < words.size()) {
            words[bit / 32] |= 1U << (bit % 32);
        }
    }
    fields.u8(static_cast<std::uint8_t>(entry.actionWords));
    for (unsigned index = 0; index < entry.actionWords; index += 2) {
        const std::uint64_t high = index + 1 < entry.actionWords ? words[index + 1] : 0;
        fields.size64(words[index] | (high << 32));
    }
    for (int axis = 0; axis < 4; ++axis) {
        fields.u8(0x80);
    }
    if (entry.stepTime != 0.0F) {
        mask |= 0x20;
        fields.f32(entry.stepTime);
    }
    if (entry.hasLook) {
        mask |= 0x40;
        fields.f32(entry.yaw);
        fields.f32(entry.pitch);
    }
    if (entry.hasCamera) {
        mask |= 0x80;
        write_position(fields, entry.camera.x, entry.camera.y, camera_height(entry.camera.z));
    }
    fields.u8(3); // The target kind 3: no target data.
    // Mask bit 8: the interaction block, after the target data (backend client_input.cpp: an entry with no bit 8 has the block
    // of the newer entry before it in the report; the newest one has no request).
    if (entry.interaction) {
        mask |= 0x100;
        fields.raw(*entry.interaction);
    } else if (before != nullptr && before->interaction) {
        mask |= 0x100;
        fields.u8(0);
    }
    if ((entry.flags & 0x1) != 0) {
        fields.size64(0);
        fields.size64(0);
        fields.size64(0);
    }
    // The mask bits after bit 8 have no fixed number: a flag takes its bits also when its field is not in the entry.
    unsigned bit = 9;
    if ((entry.flags & 0x800) != 0) {
        ++bit;
    }
    if ((entry.flags & 0x100) != 0) {
        bit += 2;
    }
    ++bit; // The value of entry +0x380: not sent (0).
    if ((entry.flags & 0x4000) != 0) {
        ++bit;
    }
    fields.size(0); // The hits of a projectile with no flight.
    fields.size(static_cast<std::uint32_t>(entry.shots.size())); // The shots.
    for (const ShotItem& shot : entry.shots) {
        write_shot(fields, shot);
    }
    fields.size(static_cast<std::uint32_t>(entry.contacts.size())); // The melee contacts.
    for (const MeleeContactItem& contact : entry.contacts) {
        write_melee_contact(fields, contact);
    }
    if ((entry.flags & kInputFlagPosition) != 0) {
        if (entry.hasCharacter) {
            mask |= 1U << bit;
            write_position(fields, entry.character.x, entry.character.y, character_height(entry.character.z));
        }
        ++bit;
    }
    if (entry.value470) {
        mask |= 1U << (bit + 1);
        fields.u8(1);
    }
    w.u8(static_cast<std::uint8_t>(mask));
    w.u8(static_cast<std::uint8_t>(mask >> 8));
    w.u8(static_cast<std::uint8_t>(mask >> 16));
    w.u16(entry.sequence);
    w.raw(fields.data());
}

} // namespace

void write_melee_contact(Writer& out, const MeleeContactItem& contact) {
    out.u32(contact.provider);
    out.raw(contact.groupActor);
    out.u64(contact.group);
    out.raw(contact.actor);
    out.u64(contact.entity);
    out.u64(contact.facet);
    write_vector(out, contact.point);
    write_vector(out, contact.normal);
    write_vector(out, contact.direction);
}

void write_shot(Writer& out, const ShotItem& shot) {
    write_vector(out, shot.origin);
    write_vector(out, shot.direction);
    write_vector(out, WorldPosition{});
    write_vector(out, shot.point);
    out.zeros(16 + 4 + 24); // The identifier, its u32, and the owner reference.
    out.raw(shot.receiverActor);
    out.u64(shot.receiver);
    out.u64(shot.facet);
    out.zeros(24 + 8 + 8 + 4 + 4); // The group, the token, a u64, and two floats.
    out.f32(shot.charge);
    out.u32(shot.ammo);
    out.u32(shot.attack);
    out.u32(shot.row);
    const bool none = std::all_of(shot.receiverActor.begin(), shot.receiverActor.end(), [](std::uint8_t byte) { return byte == 0; });
    out.zeros(2);
    out.u8(none ? 255 : 0); // The category of the receiver (wire 190).
    out.u8(0);
}

Bytes interaction_block(const InteractionRequest& request) {
    // The fields in the order of the client writer 0x35E67E0 (protocol notes 4.35); the texts empty, no vectors.
    Writer w;
    w.u8(1);
    w.raw(request.reference.actor);
    w.u64(request.reference.group);
    w.u64(request.reference.target);
    w.u8(request.action);
    w.u8(request.baseAction);
    w.u64(request.optionEntity);
    w.u32(0);
    w.string("");       // +0x48
    w.u32(0);
    w.u32(0);
    w.f32(0.0F);
    w.u8(request.committed ? 1 : 0);
    w.string("");       // +0x88 the display key of the object
    w.string("");
    w.string("");
    w.string("");
    w.f32(request.holdTime);
    w.u8(0);            // no vectors
    w.u8(0);            // +0x150
    w.u32(0);           // +0x154
    w.u8(0);            // +0x158 requiredItemCount
    w.u32(request.optionId);
    w.zeros(16);        // +0x128, +0x44, +0x160, +0x164
    w.size64(0);        // requiredCurrency
    w.u32(0);           // +0x170
    w.size(0);          // requiredLevel
    w.string("");       // +0x178
    w.zeros(5);         // +0x1A4, +0x1A8
    w.u8(0);            // +0x1A9
    w.u32(0);           // +0x1E8
    w.string("");       // +0x1C0
    w.zeros(12);        // +0x1F0, +0x1F8
    w.u32(0);           // +0x200
    w.u8(0);            // +0x204
    return w.data();
}

Bytes respawn_request_body(std::uint64_t group, bool random, const std::string& id) {
    Writer w;
    w.u64(group + kPlayerComponent);
    w.u64(group);
    w.u8(random ? 1 : 0);
    w.string(id);
    return w.data();
}

Bytes assign_pool_points_body(std::uint64_t group, std::uint32_t pool, std::uint32_t node, std::uint32_t count) {
    Writer w;
    w.u64(slice_facet_target(group, kGameEventsEntity, kProgressionPointComponent));
    w.u64(group);
    w.u32(pool);
    w.u32(node);
    w.u32(count);
    return w.data();
}

Bytes change_mapped_ability_body(std::uint64_t group, std::uint16_t tableIndex, std::uint32_t slot, std::uint32_t ability) {
    Writer w;
    w.u64(slice_facet_target(group, kStatsEntity, kAbilityComponent));
    w.u64(group);
    w.u16(tableIndex);
    w.u32(slot);
    w.u32(ability);
    return w.data();
}

Bytes apply_ability_points_body(std::uint64_t group) {
    Writer w;
    w.u64(slice_facet_target(group, kStatsEntity, kAbilityComponent));
    w.u64(group);
    return w.data();
}

Bytes mastery_tree_respec_body(std::uint64_t group, std::uint16_t tableIndex, bool flag) {
    Writer w;
    w.u64(slice_facet_target(group, kStatsEntity, kAbilityComponent));
    w.u64(group);
    w.u16(tableIndex);
    w.u8(flag ? 1 : 0);
    return w.data();
}

std::optional<RespecResponse> read_respec_response(std::span<const std::uint8_t> body) {
    Reader r(body);
    RespecResponse out;
    out.call.target = r.u64();
    out.call.entity = r.u64();
    out.success = r.u8() == 1;
    out.tableIndex = r.u32();
    if (!r.ok() || r.left() != 0) {
        return std::nullopt;
    }
    return out;
}

Bytes cutscene_ended_body(std::uint64_t group) {
    Writer w;
    w.u64(group + kPlayerCutsceneComponent);
    w.u64(group);
    return w.data();
}

Bytes interaction_cancel_body(std::uint64_t group, const InteractionReference& reference) {
    Writer w;
    w.u64(group + kInteractorComponent);
    w.u64(group);
    w.raw(reference.actor);
    w.u64(reference.group);
    w.u64(reference.target);
    return w.data();
}

std::uint64_t crafting_target(std::uint64_t group) {
    // The rule of protocol notes 4.2: the instance of the entity (entity XOR root XOR G) plus the component id.
    return (kCraftingEntity ^ kPlayerSliceRoot ^ group) + kCraftingComponent;
}

Bytes craft_request_body(std::uint64_t group, const CraftRequest& request) {
    Writer w;
    w.u64(crafting_target(group));
    w.u64(group);
    w.u32(request.recipe);
    w.u32(request.quantity);
    w.u32(request.coinFee);
    w.u16(request.gearScore);
    w.size(static_cast<std::uint32_t>(request.perkItems.size()));
    for (const std::uint32_t perk : request.perkItems) {
        w.u32(perk);
    }
    w.u8(request.upgrade);
    w.size(static_cast<std::uint32_t>(request.items.size()));
    for (const std::uint32_t item : request.items) {
        w.u8(1);       // eIngredientType_Item (the client converts each selection to an item entry: 0x5BA9FB0)
        w.u32(1);      // the quantity of an entry (the server takes the quantities of the recipe)
        w.u32(item);
    }
    return w.data();
}

Bytes auto_pinning_body(std::uint64_t group, const std::vector<bool>& flags) {
    Writer w;
    w.u64(group + kObjectivesComponent);
    w.u64(group);
    const std::size_t count = std::min(flags.size(), kAutoPinFlags);
    w.size(static_cast<std::uint32_t>(count));
    for (std::size_t index = 0; index < count; ++index) {
        w.u8(flags[index] ? 1 : 0);
    }
    return w.data();
}

Bytes tracked_objectives_body(std::uint64_t group, const std::vector<std::uint64_t>& instances) {
    Writer w;
    w.u64(group + kObjectivesComponent);
    w.u64(group);
    const std::size_t count = std::min(instances.size(), kTrackedLimit);
    w.size(static_cast<std::uint32_t>(count));
    for (std::size_t index = 0; index < count; ++index) {
        w.u64(instances[index]);
    }
    return w.data();
}

Bytes open_ui_screen_body(std::uint64_t group, std::uint32_t screen) {
    Writer w;
    w.u64(group + kObjectivesComponent);
    w.u64(group);
    w.u32(screen);
    return w.data();
}

Bytes objective_instance_body(std::uint64_t group, std::uint64_t instance) {
    Writer w;
    w.u64(group + kObjectivesComponent);
    w.u64(group);
    w.u64(instance);
    return w.data();
}

Bytes conversation_topic_body(std::uint64_t group, std::uint32_t topic) {
    Writer w;
    w.u64(group + kConversationComponent);
    w.u64(group);
    w.u32(topic);
    return w.data();
}

Bytes finished_topics_body(std::uint64_t group, const std::vector<std::uint32_t>& topics) {
    Writer w;
    w.u64(group + kConversationComponent);
    w.u64(group);
    const std::size_t count = std::min(topics.size(), kFinishedTopicsLimit);
    w.size(static_cast<std::uint32_t>(count));
    for (std::size_t index = 0; index < count; ++index) {
        w.u32(topics[index]);
    }
    return w.data();
}

Bytes territory_report_body(std::uint64_t group, const std::array<std::uint8_t, 16>& actor, std::uint64_t referenceGroup, std::uint64_t local,
                            std::uint16_t territory, std::optional<std::uint32_t> achievement) {
    Writer w;
    w.u64(group + kTerritoryDetectorComponent);
    w.u64(group);
    for (const std::uint8_t byte : actor) {
        w.u8(byte);
    }
    w.u64(referenceGroup);
    w.u64(local);
    w.u16(territory);
    if (achievement) {
        w.u32(*achievement);
    }
    return w.data();
}

Bytes craft_cancel_body(std::uint64_t group) {
    Writer w;
    w.u64(crafting_target(group));
    w.u64(group);
    return w.data();
}

std::optional<CraftProgress> read_craft_progress(std::span<const std::uint8_t> body) {
    Reader reader(body);
    CraftProgress progress;
    progress.call.target = reader.u64();
    progress.call.entity = reader.u64();
    progress.recipe = reader.string();
    progress.completed = reader.u32();
    progress.total = reader.u32();
    if (!reader.ok()) {
        return std::nullopt;
    }
    return progress;
}

std::optional<ComponentCall> read_component_call(std::span<const std::uint8_t> body) {
    Reader reader(body);
    ComponentCall call;
    call.target = reader.u64();
    call.entity = reader.u64();
    if (!reader.ok()) {
        return std::nullopt;
    }
    return call;
}

std::optional<PlayerDeath> read_player_death(std::span<const std::uint8_t> body) {
    Reader reader(body);
    PlayerDeath death;
    death.call.target = reader.u64();
    death.call.entity = reader.u64();
    death.time = reader.f32();
    if (!reader.ok()) {
        return std::nullopt;
    }
    return death;
}

std::optional<DamageTaken> read_damage_taken(std::span<const std::uint8_t> body) {
    Reader reader(body);
    DamageTaken damage;
    damage.call.target = reader.u64();
    damage.call.entity = reader.u64();
    damage.source = reader.u64();
    // The flags are two raw bytes, the low byte first.
    const std::uint8_t low = reader.u8();
    const std::uint8_t high = reader.u8();
    damage.flags = static_cast<std::uint16_t>(low | (high << 8));
    damage.part = reader.f32();
    for (float& value : damage.from) {
        value = reader.f32();
    }
    const std::uint32_t count = reader.size();
    // An entry has 9 bytes: a count above the rest of the body is no message.
    if (!reader.ok() || count > reader.left() / 9) {
        return std::nullopt;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        DamageTaken::Entry entry;
        entry.type = reader.u8();
        entry.amount = reader.f32();
        entry.part = reader.f32();
        damage.entries.push_back(entry);
    }
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return damage;
}

std::optional<InteractionResult> read_interaction_result(std::span<const std::uint8_t> body) {
    Reader reader(body);
    InteractionResult result;
    result.call.target = reader.u64();
    result.call.entity = reader.u64();
    for (std::uint8_t& value : result.reference.actor) {
        value = reader.u8();
    }
    result.reference.group = reader.u64();
    result.reference.target = reader.u64();
    result.action = reader.u8();
    result.result = reader.u8();
    if (!reader.ok()) {
        return std::nullopt;
    }
    return result;
}

std::uint64_t global_storage_target(std::uint64_t group) {
    return (kGlobalStorageEntity ^ kPlayerSliceRoot ^ group) + kGlobalStorageContainer;
}

namespace {
void write_bag_reference(Writer& w, const ActorId& actor) {
    const std::uint64_t group = group_of(actor);
    w.u64(group + kPlayerTradeComponent);
    w.u64(group);
    w.raw(actor);
    w.u64(group);
    w.u64(global_storage_target(group));
}

/** The head of a replicated map in its full form (protocol notes 4.41): changes 0, the version, the count. */
bool read_map_head(Reader& reader, std::uint32_t& count) {
    if (reader.size() != 0) {
        return false;
    }
    if (reader.u8() == 1) {
        reader.size64();
    }
    count = reader.size();
    return reader.ok();
}

ActorId read_actor(Reader& reader) {
    ActorId actor{};
    for (std::uint8_t& value : actor) {
        value = reader.u8();
    }
    return actor;
}
} // namespace

Bytes take_all_items_body(const ActorId& actor) {
    Writer w;
    write_bag_reference(w, actor);
    return w.data();
}

Bytes take_item_body(const ActorId& actor, const std::array<std::uint8_t, 16>& instance, std::uint32_t quantity) {
    Writer w;
    write_bag_reference(w, actor);
    w.raw(instance);
    w.u32(quantity);
    return w.data();
}

std::uint64_t inventory_target(std::uint64_t group, std::uint64_t component) {
    return (kInventoryEntity ^ kPlayerSliceRoot ^ group) + component;
}

Bytes equip_item_body(std::uint64_t group, std::int32_t bagSlot, std::uint32_t paperdollSlot, std::uint32_t quantity, const ItemInstanceId& instance) {
    Writer w;
    w.u64(inventory_target(group, kPaperdollComponent));
    w.u64(group);
    w.u32(static_cast<std::uint32_t>(bagSlot));
    w.u32(paperdollSlot);
    w.u32(quantity);
    w.raw(instance);
    w.u8(0);
    return w.data();
}

Bytes unequip_item_body(std::uint64_t group, std::uint32_t paperdollSlot, std::int32_t bagSlot, std::uint32_t quantity) {
    Writer w;
    w.u64(inventory_target(group, kPaperdollComponent));
    w.u64(group);
    w.u32(paperdollSlot);
    w.u32(static_cast<std::uint32_t>(bagSlot));
    w.u32(quantity);
    w.u8(1);
    return w.data();
}

Bytes swap_items_body(std::uint64_t group, std::uint32_t slotTo, std::uint32_t slotFrom, const ItemInstanceId& instanceTo,
                      const ItemInstanceId& instanceFrom) {
    Writer w;
    w.u64(inventory_target(group, kPaperdollComponent));
    w.u64(group);
    w.u32(slotTo);
    w.u32(slotFrom);
    w.raw(instanceTo);
    w.raw(instanceFrom);
    w.u8(0);
    return w.data();
}

Bytes use_item_body(std::uint64_t group, std::uint32_t bagSlot) {
    Writer w;
    w.u64(inventory_target(group, kInventoryContainer));
    w.u64(group);
    w.u32(bagSlot);
    return w.data();
}

Bytes drop_inventory_items_body(std::uint64_t group, const std::vector<DropItemEntry>& entries) {
    Writer w;
    w.u64(group + kItemDropComponent);
    w.u64(group);
    w.size(static_cast<std::uint32_t>(entries.size()));
    for (const DropItemEntry& entry : entries) {
        w.u32(entry.slot);
        w.raw(entry.instance);
        w.u32(0xFFFFFFFFU);   // a slot value of the client whose meaning is not known (the client sends FFFFFFFF)
        w.u32(entry.quantity);
    }
    return w.data();
}

Bytes drop_paperdoll_item_body(std::uint64_t group, std::uint32_t paperdollSlot, std::uint32_t quantity) {
    Writer w;
    w.u64(group + kItemDropComponent);
    w.u64(group);
    w.u32(paperdollSlot);
    w.u32(quantity);
    return w.data();
}

std::optional<std::uint64_t> read_currency(std::span<const std::uint8_t> field) {
    Reader reader(field);
    const std::uint64_t coin = reader.u64();
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return coin;
}

std::optional<std::vector<LootClaim>> read_loot_tracker(std::span<const std::uint8_t> field) {
    Reader reader(field);
    std::uint32_t count = 0;
    if (!read_map_head(reader, count)) {
        return std::nullopt;
    }
    std::vector<LootClaim> claims;
    for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
        // actor 16, state u32, kind u32, resetTime u64, lootedTime u64, privileged u8, x y z f32, tag u32, selector u8 (58 bytes).
        LootClaim claim;
        claim.actor = read_actor(reader);
        claim.state = reader.u32();
        claim.kind = reader.u32();
        claim.resetTime = reader.u64();
        claim.lootedTime = reader.u64();
        claim.privileged = reader.u8() != 0;
        claim.place.x = reader.f32();
        claim.place.y = reader.f32();
        claim.place.z = reader.f32();
        claim.tag = reader.u32();
        claim.selector = reader.u8();
        claims.push_back(claim);
    }
    if (!reader.ok()) {
        return std::nullopt;
    }
    return claims;
}

std::optional<std::vector<StorageKey>> read_global_storage(std::span<const std::uint8_t> field) {
    Reader reader(field);
    std::uint32_t count = 0;
    if (!read_map_head(reader, count)) {
        return std::nullopt;
    }
    std::vector<StorageKey> keys;
    for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
        StorageKey key;
        key.actor = read_actor(reader);
        const std::uint32_t items = reader.size();
        for (std::uint32_t item = 0; item < items && reader.ok(); ++item) {
            soulwarden::backend::tests::WireItem value;
            if (!soulwarden::backend::tests::read_item(reader, value)) {
                return std::nullopt;
            }
            key.items.push_back(value);
        }
        keys.push_back(std::move(key));
    }
    if (!reader.ok()) {
        return std::nullopt;
    }
    return keys;
}

std::optional<std::vector<std::uint8_t>> read_bit_vector(std::span<const std::uint8_t> field) {
    // The full form of a vector: changes:size (0), version (has:u8, sequence:size64), count:size, the bytes.
    Reader reader(field);
    std::uint32_t count = 0;
    if (!read_map_head(reader, count)) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes;
    for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
        bytes.push_back(reader.u8());
    }
    if (!reader.ok()) {
        return std::nullopt;
    }
    return bytes;
}

bool has_bit(const std::vector<std::uint8_t>& bytes, std::uint32_t index) {
    return (index >> 3) < bytes.size() && (bytes[index >> 3] & (1U << (index & 7))) != 0;
}

bool apply_gathering(const soulwarden::backend::tests::Fragment& fragment, GatheringState& state) {
    namespace decoder = soulwarden::backend::tests;
    constexpr unsigned kGroup = 1;
    const auto number = [&fragment](unsigned field, std::size_t size, std::uint64_t& out) {
        const auto value = decoder::field_of(fragment, kGroup, field);
        if (value.size() != size) {
            return false;
        }
        out = 0;
        for (const std::uint8_t byte : value) {
            out = (out << 8) | byte;
        }
        return true;
    };
    const auto real = [](std::uint64_t bits) {
        const auto narrow = static_cast<std::uint32_t>(bits);
        float out = 0.0F;
        std::memcpy(&out, &narrow, sizeof(out));
        return out;
    };
    bool changed = false;
    std::uint64_t reference = 0;
    if (number(4, 4, reference) && static_cast<std::uint32_t>(reference) != state.reference) {
        state.reference = static_cast<std::uint32_t>(reference);
        std::uint64_t value = 0;
        if (number(0, 1, value)) {
            state.status = static_cast<std::uint8_t>(value);
        }
        if (number(1, 4, value)) {
            state.remaining = real(value);
        }
        if (number(2, 4, value)) {
            state.speed = real(value);
        }
        if (number(3, 1, value)) {
            state.gatherers = static_cast<std::uint8_t>(value);
        }
        changed = true;
    }
    std::uint64_t sync = 0;
    if (number(8, 4, sync) && static_cast<std::uint32_t>(sync) != state.syncReference) {
        state.syncReference = static_cast<std::uint32_t>(sync);
        std::uint64_t value = 0;
        if (number(6, 1, value)) {
            state.active = value != 0;
        }
        if (number(7, 8, value)) {
            state.target = value;
        }
        changed = true;
    }
    return changed;
}

bool apply_container(std::span<const std::uint8_t> field, std::vector<soulwarden::backend::tests::WireItem>& elements) {
    const soulwarden::backend::tests::WireContainer container = soulwarden::backend::tests::read_container(field);
    if (!container.ok) {
        return false;
    }
    if (container.full) {
        elements.clear();
    }
    for (const auto& [index, item] : container.elements) {
        if (index < elements.size()) {
            elements[index] = item;
        } else if (index == elements.size()) {
            elements.push_back(item);
        }
    }
    return true;
}

std::uint64_t group_of(const ActorId& actor) {
    std::uint64_t value = 0;
    for (int index = 7; index >= 0; --index) {
        value = (value << 8) | actor[static_cast<std::size_t>(index)];
    }
    return value;
}

std::string hex_of(std::span<const std::uint8_t> bytes) {
    return hex_text(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

bool read_server_frames(std::span<const std::uint8_t> data, std::vector<ServerMessage>& out, std::string& problem) {
    Reader reader(data);
    while (reader.ok() && reader.left() > 0) {
        const std::uint32_t size = reader.size();
        const auto envelope = reader.raw(size);
        if (!reader.ok()) {
            problem = "a server frame is longer than its message";
            return false;
        }
        Reader message(envelope);
        const std::uint8_t flags = message.u8();
        if ((flags & kEnvelopeTarget) != 0) {
            message.raw(8);
        }
        if ((flags & kEnvelopeSource) != 0) {
            message.raw(8);
        }
        if ((flags & kEnvelopeExtension) != 0) {
            problem = "an envelope with an extension (flags " + std::to_string(flags) + ")";
            return false;
        }
        const std::uint8_t present = message.u8();
        if (!message.ok() || present != 1) {
            problem = "an envelope with no message";
            return false;
        }
        ServerMessage item;
        item.type = message.size();
        if (!message.ok()) {
            problem = "an envelope with no type";
            return false;
        }
        const auto body = envelope.subspan(message.position());
        item.body.assign(body.begin(), body.end());
        out.push_back(std::move(item));
    }
    return reader.ok();
}

Bytes client_frame(const ActorId& route, std::uint32_t type, std::span<const std::uint8_t> body) {
    Writer payload;
    payload.raw(route);
    payload.u8(0); // Envelope flags.
    payload.u8(1); // A message is present.
    payload.size(type);
    payload.raw(body);
    return soulwarden::backend::server::hub::encode_frame(payload.data());
}

std::string local_ticket_hex(const std::string& steamId, const std::string& personaName) {
    const std::string personaHex = hex_text(std::string_view(personaName).substr(0, 128));
    return hex_text("SOULWARDEN-LOCAL-TICKET;1;" + steamId + ";" + personaHex);
}

Bytes registration_body(const LoginToken& token, const std::string& steamId, const std::string& ticketHex) {
    Writer w;
    w.u32(1);
    w.size(6); // The version map.
    for (std::uint32_t key = 0; key < 6; ++key) {
        w.u32(key);
        w.string(kClientVersion);
    }
    w.string("");
    // The login token (writers 0x7D5F30 to 0x7D42C0).
    w.string(token.signature);
    w.string(token.repAddress);
    w.string(token.worldId);
    w.string(token.characterId);
    w.string(token.personaId);
    w.u32(token.steamAppId);
    w.string(token.steamUserId);
    w.string(token.channelId);
    w.string(token.ticketId);
    w.string("");
    w.string("");
    w.u32(token.issueTime);
    w.u32(token.generateTime);
    w.u32(token.accountAge);
    w.string("");
    w.u32(token.tokenVersion);
    w.u8(0);
    w.string("");
    w.u8(1); // present, with the flag 0 (the captures).
    w.u8(0);
    w.string("");
    w.string("");
    w.string("");
    // The client data (writer 0x7D54A0): u32 1, u8 1, the Steam application, the Steam id, the ticket.
    w.u32(1);
    w.u8(1);
    w.u32(token.steamAppId);
    w.string(steamId);
    w.string("steam|" + ticketHex);
    w.string("");
    w.u32(0);
    for (int index = 0; index < 5; ++index) {
        w.string("");
    }
    // Two identities in the text form and one flag.
    w.u8(0);
    w.string("");
    w.u8(0);
    w.string("");
    w.u8(0);
    return w.data();
}

std::optional<RegistrationResponse> read_registration_response(std::span<const std::uint8_t> body) {
    Reader reader(body);
    RegistrationResponse response;
    response.result = static_cast<std::int32_t>(reader.u32());
    response.serverTime = reader.u64();
    response.serverVersion = reader.string();
    reader.string();
    reader.u8();
    reader.u8();
    reader.u8();
    response.antiCheatRequired = reader.u8() != 0;
    if (!reader.ok()) {
        return std::nullopt;
    }
    return response;
}

const char* registration_result_text(std::int32_t result) {
    switch (result) {
    case 0:
        return "accepted";
    case 37:
        return "CapacityLimitReached";
    case -10:
        return "ClaimConnectionTicketFailed";
    case -80:
        return "VersionCheckFailed";
    case -90:
        return "TypeIndexMismatch";
    case -160:
        return "LoginTicketInvalid";
    case -200:
        return "EasyAntiCheatFailure";
    case -210:
        return "EOSIsRequired";
    case -900:
        return "HubNotReady";
    default:
        return "";
    }
}

std::optional<SelfIdentification> read_self_identification(std::span<const std::uint8_t> body) {
    Reader reader(body);
    SelfIdentification message;
    if (!read_identity(reader, &message.sessionActor) || !read_reference(reader, message.playerManager)
        || !read_reference(reader, message.clientProxy) || !read_reference(reader, message.third)) {
        return std::nullopt;
    }
    // The level block: instance:u32 list<u32> flag flag u32 u32.
    message.instance = reader.u32();
    const std::uint32_t count = reader.size();
    for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
        reader.u32();
    }
    reader.u8();
    reader.u8();
    reader.u32();
    reader.u32();
    if (!read_identity(reader, nullptr) || reader.left() != 0) {
        return std::nullopt;
    }
    return message;
}

std::optional<LevelInfo> read_level_info(std::span<const std::uint8_t> body) {
    Reader reader(body);
    LevelInfo info;
    info.level = reader.string();
    info.terrain = reader.string();
    for (int index = 0; index < 4; ++index) {
        reader.u32();
    }
    reader.u64();
    // A list of {u8, u32, u64, u32}.
    const std::uint32_t count = reader.size();
    for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
        reader.u8();
        reader.u32();
        reader.u64();
        reader.u32();
    }
    reader.u8();
    info.instance = reader.u8();
    info.reload = reader.u8() != 0;
    reader.u8();
    info.version = reader.u64();
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return info;
}

std::optional<std::uint32_t> read_reason(std::span<const std::uint8_t> body) {
    Reader reader(body);
    const std::uint32_t reason = reader.u32();
    if (!reader.ok()) {
        return std::nullopt;
    }
    return reason;
}

WorldPosition read_world_position(std::span<const std::uint8_t> value) {
    Reader reader(value);
    WorldPosition position;
    position.x = reader.f32();
    position.y = reader.f32();
    position.z = kHeightMin + static_cast<float>(reader.u16()) / 65535.0F * kHeightRange;
    return position;
}

WorldPosition read_object_place(std::span<const std::uint8_t> value) {
    WorldPosition out;
    if (value.size() < 10) {
        return out;
    }
    Reader reader(value);
    out.x = reader.f32();
    out.y = reader.f32();
    out.z = -100.0F + static_cast<float>(reader.u16()) / 65535.0F * 2100.0F;
    return out;
}

WorldPosition position_field(const WorldPosition& character) {
    return WorldPosition{character.x, character.y, kHeightMin + static_cast<float>(character_height(character.z)) / 65535.0F * kHeightRange};
}

Quaternion read_place_rotation(std::span<const std::uint8_t> value) {
    Quaternion out;
    if (value.empty()) {
        return out;
    }
    Reader reader(value);
    const std::uint8_t header = reader.u8();
    float axes[3]{};
    float sum = 0.0F;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if ((header & (0x01U << axis)) != 0) {
            axes[axis] = 0.0F;
        } else if ((header & (0x08U << axis)) != 0) {
            axes[axis] = 1.0F;
        } else {
            axes[axis] = static_cast<float>(reader.u16()) / 32767.5F - 1.0F;
        }
        sum += axes[axis] * axes[axis];
    }
    if (!reader.ok()) {
        return Quaternion{};
    }
    out.x = axes[0];
    out.y = axes[1];
    out.z = axes[2];
    out.w = std::sqrt(std::max(0.0F, 1.0F - sum));
    if ((header & 0x40U) != 0) {
        out.w = -out.w;
    }
    return out;
}

Quaternion read_quaternion(std::span<const std::uint8_t> value) {
    constexpr float kStep = 0.0055459356F;
    constexpr float kLowest = 0.70710677F;
    Quaternion out;
    if (value.empty()) {
        return out;
    }
    const std::uint8_t header = value[0];
    const unsigned largest = (header >> 5) & 3U;
    float components[4]{};
    float sum = 0.0F;
    std::size_t next = 1;
    for (unsigned component = 0; component < 4; ++component) {
        if (component == largest || (header & (2U << component)) != 0) {
            continue;
        }
        const float part = next < value.size() ? static_cast<float>(value[next]) * kStep - kLowest : 0.0F;
        ++next;
        components[component] = part;
        sum += part * part;
    }
    const float big = std::sqrt(std::max(0.0F, 1.0F - sum));
    components[largest] = (header & 1U) != 0 ? -big : big;
    out.x = components[0];
    out.y = components[1];
    out.z = components[2];
    out.w = components[3];
    return out;
}

CheckedBundle read_state_bundle(std::span<const std::uint8_t> body) {
    namespace decoder = soulwarden::backend::tests;
    CheckedBundle out;
    decoder::Bundle& bundle = out.bundle;
    Reader reader(body);
    if (reader.u8() != 1) {
        return out;
    }
    bundle.sequence = reader.size64();
    bundle.instance = reader.u8();
    reader.u8();
    const std::uint8_t unreliable = reader.u8();
    const std::uint8_t control = reader.u8();
    if (unreliable > 1 || control > 1) {
        return out;
    }
    if (control == 1) {
        // The control block: stopCount:size count:size (interest:u16)*count.
        const std::uint32_t stopCount = reader.size();
        const std::uint32_t count = reader.size();
        if (stopCount > count || count > 100) {
            return out;
        }
        for (std::uint32_t index = 0; index < count; ++index) {
            (index < stopCount ? bundle.stops : bundle.pauses).push_back(reader.u16());
        }
    }
    const std::uint32_t size = reader.size();
    if (!reader.ok() || reader.position() + size != body.size()) {
        return out;
    }
    out.headerOk = true;
    const auto payload = body.subspan(reader.position());
    std::size_t position = 0;
    while (position < payload.size()) {
        Reader head(payload.subspan(position));
        decoder::Record record;
        record.interest = head.size();
        const unsigned count = head.u8();
        if (!head.ok()) {
            return out;
        }
        position += head.position();
        for (unsigned index = 0; index < count; ++index) {
            Reader fragmentHead(payload.subspan(position));
            decoder::Fragment fragment;
            fragment.slot = fragmentHead.size();
            fragment.type = fragmentHead.size();
            position += fragmentHead.position();
            if (!fragmentHead.ok() || !decoder::detail::read_state(payload, position, fragment)) {
                out.failedInterest = record.interest;
                out.failedSlot = fragment.slot;
                out.failedType = fragment.type;
                if (!record.fragments.empty()) {
                    bundle.records.push_back(std::move(record));
                }
                return out;
            }
            record.fragments.push_back(std::move(fragment));
        }
        bundle.records.push_back(std::move(record));
    }
    out.complete = position == payload.size();
    bundle.complete = out.complete;
    return out;
}

Bytes input_report_body(const std::vector<InputEntry>& entries, std::uint64_t target, std::uint64_t entity) {
    Writer w;
    w.u64(target);
    w.u64(entity);
    // The header: 4 bytes, 3 numbers, u8, the optional flag, u64, u8, 3 numbers, u8, the pair count, 4 numbers.
    w.zeros(4);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.u8(0);
    w.u8(0);
    w.u64(0);
    w.u8(0);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.u8(0);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.u8(static_cast<std::uint8_t>(entries.size()));
    for (std::size_t index = 0; index < entries.size(); ++index) {
        write_input_entry(w, entries[index], index == 0 ? nullptr : &entries[index - 1]);
    }
    return w.data();
}

} // namespace swc::net
