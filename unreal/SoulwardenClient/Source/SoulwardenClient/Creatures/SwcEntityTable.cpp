#include "Creatures/SwcEntityTable.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace swc::creatures {
namespace {

namespace decoder = soulwarden::backend::tests;
using swc::net::Reader;

/** State types (the type index of the backend, rep_session.h; protocol notes 4.14). */
constexpr std::uint32_t kTypeMetadata = 10;
constexpr std::uint32_t kTypeMovement = 11;
constexpr std::uint32_t kTypeVitals = 15;
constexpr std::uint32_t kTypeMarker = 5399;
constexpr std::uint32_t kTypeProjectile = 16;
constexpr std::uint32_t kTypeSpell = 2912;
constexpr std::uint32_t kTypePositionInTheWorld = 13;
constexpr std::uint32_t kTypeDelayedEvent = 3139;
constexpr std::uint32_t kTypeLootDrop = 2027;
constexpr std::uint32_t kTypeInteract = 2930;
constexpr std::uint32_t kTypeStatusEffects = 4236;
constexpr std::uint32_t kTypeGatherable = 12;
// The compact map of the StatusEffects state (backend server/rep/states/status_state.cpp): group 3 field 1.
constexpr unsigned kStatusObserverGroup = 3;
constexpr unsigned kStatusCompactMap = 1;
constexpr std::uint8_t kPackedPotencyOne = 0x80;
// The fields of the Projectile state (backend server/rep/states/projectile_state.cpp).
constexpr unsigned kShotPosition = 0;
constexpr unsigned kShotVelocity = 2;
constexpr unsigned kShotTime = 4;
constexpr unsigned kShotFlags = 9;
constexpr unsigned kShotOwner = 10;
constexpr unsigned kShotAmmo = 13;
constexpr std::uint8_t kShotCollided = 0x01;
// The fields of the Spell state (backend server/rep/states/spell_state.cpp).
constexpr unsigned kSpellKey = 0;
constexpr unsigned kSpellPlace = 1;
constexpr unsigned kSpellRotation = 2;
constexpr unsigned kSpellAttachment = 6;
constexpr unsigned kSpellCaster = 7;
constexpr unsigned kSpellSpawn = 8;
constexpr unsigned kSpellRadiusScale = 10;
constexpr std::uint32_t kSlotMetadata = 0;

/** Fields of the movement state (movement_state.cpp). */
constexpr unsigned kRotation = 11;
constexpr unsigned kWorldPosition = 27;
constexpr unsigned kScopeTimeLow = 28;
constexpr unsigned kScopeTimeBlob = 29;
constexpr unsigned kTeleport = 31;
constexpr unsigned kScopeData = 32;
constexpr unsigned kScopeInfoBlob = 33;
constexpr unsigned kGlobalFragTags = 34;
constexpr unsigned kScopeTimeByte1 = 38;
constexpr unsigned kTimeAnchor = 40;
constexpr unsigned kDataBits = 41;
constexpr unsigned kScopeTimeByte2 = 49;
constexpr unsigned kScopeTimeByte3 = 50;

/** Fields of the vitals state (vitals_state.cpp). */
constexpr unsigned kHealthAmount = 0;
constexpr unsigned kVitalsData = 2;
constexpr unsigned kHealthChangeFlags = 3;
constexpr unsigned kHealthMax = 4;
constexpr unsigned kVitalsId = 8;
constexpr unsigned kVitalsCategoryId = 9;
constexpr unsigned kVitalsLevel = 10;
constexpr unsigned kInvulnerability = 11;

std::uint64_t read_be(std::span<const std::uint8_t> value) {
    std::uint64_t out = 0;
    for (const std::uint8_t byte : value) {
        out = (out << 8) | byte;
    }
    return out;
}

float f32_at(std::span<const std::uint8_t> value) {
    Reader reader(value);
    return reader.f32();
}

/** @return A number of a value in the size code (the field scopeData). */
std::uint32_t size_of(std::span<const std::uint8_t> value) {
    Reader reader(value);
    return reader.size();
}

/** @return A place of the Projectile, Spell, and PositionInTheWorld states: x:f32 y:f32 z:u16 over -100 to 2000 m (reader
 *  0x5CEA190; the movement state has -100 to 1000 m). */
swc::net::WorldPosition wide_position(std::span<const std::uint8_t> value) {
    constexpr float kWideHeightMin = -100.0F;
    constexpr float kWideHeightRange = 2100.0F;
    swc::net::WorldPosition position;
    position.x = f32_at(value.subspan(0, 4));
    position.y = f32_at(value.subspan(4, 4));
    position.z = kWideHeightMin + static_cast<float>(read_be(value.subspan(8, 2))) / 65535.0F * kWideHeightRange;
    return position;
}

} // namespace

float half_to_float(std::uint16_t half) {
    const std::uint32_t sign = (half >> 15) & 1U;
    const std::uint32_t exponent = (half >> 10) & 0x1FU;
    const std::uint32_t fraction = half & 0x3FFU;
    float value = 0.0F;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(fraction), -24);
    } else if (exponent == 31) {
        value = fraction == 0 ? INFINITY : NAN;
    } else {
        value = std::ldexp(static_cast<float>(fraction | 0x400U), static_cast<int>(exponent) - 25);
    }
    return sign != 0 ? -value : value;
}

std::string Entity::slice_key() const {
    // The asset id of the metadata state is the GUID in the byte order of its text (protocol notes 4.14, the record of the
    // Grey Wolf; the asset texts of creatures.json).
    char text[48];
    const auto& b = sliceAsset;
    std::snprintf(text, sizeof(text), "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X:%u", b[0], b[1], b[2], b[3], b[4],
                  b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15], sliceSubId);
    return text;
}

const Entity* EntityTable::find(std::uint32_t interest) const {
    const auto found = m_entities.find(interest);
    return found != m_entities.end() ? &found->second : nullptr;
}

std::vector<Change> EntityTable::take_changes() {
    std::vector<Change> out;
    out.swap(m_changes);
    return out;
}

void EntityTable::clear() {
    for (const auto& [interest, entity] : m_entities) {
        m_changes.push_back({Change::Kind::removed, interest, entity.serial});
    }
    m_entities.clear();
}

void EntityTable::stop(std::uint32_t interest) {
    const auto found = m_entities.find(interest);
    if (found == m_entities.end()) {
        return;
    }
    m_changes.push_back({Change::Kind::removed, interest, found->second.serial});
    m_entities.erase(found);
}

void EntityTable::apply(const decoder::Record& record, std::uint64_t nowUs) {
    auto found = m_entities.find(record.interest);
    if (found == m_entities.end()) {
        Entity entity;
        entity.interest = record.interest;
        entity.serial = ++m_serial;
        found = m_entities.emplace(record.interest, std::move(entity)).first;
        m_changes.push_back({Change::Kind::added, record.interest, found->second.serial});
    }
    Entity& entity = found->second;
    for (const decoder::Fragment& fragment : record.fragments) {
        if (fragment.slot == kSlotMetadata && fragment.type == kTypeMetadata) {
            const auto asset = decoder::field_of(fragment, 0, 0);
            const auto actor = decoder::field_of(fragment, 0, 1);
            if (asset.size() == 20 && actor.size() == 16) {
                std::copy(asset.begin(), asset.begin() + 16, entity.sliceAsset.begin());
                entity.sliceSubId = static_cast<std::uint32_t>(read_be(asset.subspan(16, 4)));
                std::copy(actor.begin(), actor.end(), entity.actor.begin());
                entity.hasMetadata = true;
            }
        } else if (fragment.type == kTypeMovement) {
            entity.movementSlot = fragment.slot;
            apply_movement(fragment, entity);
        } else if (fragment.type == kTypeVitals) {
            apply_vitals(fragment, entity);
        } else if (fragment.type == kTypeMarker) {
            if (const auto value = decoder::field_of(fragment, 0, 0); value.size() == 1) {
                entity.hasMarker = true;
                entity.markerEnabled = value[0] != 0;
            }
        } else if (fragment.type == kTypeProjectile) {
            apply_projectile(fragment, entity);
        } else if (fragment.type == kTypeSpell || fragment.type == kTypeDelayedEvent) {
            apply_spell(fragment, entity, nowUs);
        } else if (fragment.type == kTypePositionInTheWorld) {
            if (const auto place = decoder::field_of(fragment, 0, 0); place.size() == 10) {
                entity.hasWorldPlace = true;
                entity.worldPlace = wide_position(place);
            }
            if (const auto rotation = decoder::field_of(fragment, 0, 1); !rotation.empty()) {
                entity.worldPlaceRotation = swc::net::read_quaternion(rotation);
            }
            m_changes.push_back({Change::Kind::effect, entity.interest, entity.serial});
        } else if (fragment.type == kTypeLootDrop) {
            if (const auto rarity = decoder::field_of(fragment, 0, 0); rarity.size() == 4) {
                entity.hasLootDrop = true;
                entity.lootRarity = static_cast<std::uint32_t>(read_be(rarity));
            }
            m_changes.push_back({Change::Kind::effect, entity.interest, entity.serial});
        } else if (fragment.type == kTypeStatusEffects) {
            apply_statuses(fragment, entity);
        } else if (fragment.type == kTypeInteract) {
            Entity::InteractState& state = entity.interact[fragment.slot];
            if (const auto enabled = decoder::field_of(fragment, 0, 0); enabled.size() == 1) {
                state.enabled = enabled[0] != 0;
            }
            if (const auto users = decoder::field_of(fragment, 0, 1); users.size() == 4) {
                state.users = static_cast<std::uint32_t>(read_be(users));
            }
            m_changes.push_back({Change::Kind::effect, entity.interest, entity.serial});
        } else if (fragment.type == kTypeGatherable) {
            if (const auto node = decoder::field_of(fragment, 0, 0); node.size() == 1) {
                entity.hasGatherable = true;
                entity.gatherableState = node[0];
                entity.gatherableSlot = fragment.slot;
                ++entity.gatherableRevision;
            }
            m_changes.push_back({Change::Kind::effect, entity.interest, entity.serial});
        }
    }
}

void EntityTable::apply_projectile(const decoder::Fragment& fragment, Entity& entity) {
    // A launch (flags 0: the flight from the anchor) or a collision (flags bit 0: the contact place and its time). A first
    // record after a contact has both, in this order.
    const auto flags = decoder::field_of(fragment, 0, kShotFlags);
    const auto place = decoder::field_of(fragment, 0, kShotPosition);
    const auto time = decoder::field_of(fragment, 0, kShotTime);
    Projectile& shot = entity.projectile;
    if (!flags.empty() && (flags[0] & kShotCollided) != 0) {
        shot.collided = true;
        if (place.size() == 10) {
            shot.contact = wide_position(place);
        }
        if (time.size() == 8) {
            shot.contactUs = read_be(time);
        }
    } else {
        shot.hasLaunch = true;
        if (place.size() == 10) {
            shot.anchor = wide_position(place);
        }
        if (const auto velocity = decoder::field_of(fragment, 0, kShotVelocity); velocity.size() == 6) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                shot.velocity[axis] = half_to_float(static_cast<std::uint16_t>(read_be(velocity.subspan(axis * 2, 2))));
            }
        }
        if (time.size() == 8) {
            shot.launchUs = read_be(time);
        }
        if (const auto owner = decoder::field_of(fragment, 0, kShotOwner); owner.size() == 8) {
            shot.owner = read_be(owner);
        }
        if (const auto ammo = decoder::field_of(fragment, 0, kShotAmmo); !ammo.empty()) {
            shot.ammoKey = size_of(ammo);
        }
    }
    entity.hasProjectile = true;
    m_changes.push_back({Change::Kind::effect, entity.interest, entity.serial});
}

void EntityTable::apply_statuses(const decoder::Fragment& fragment, Entity& entity) {
    const auto map = decoder::field_of(fragment, kStatusObserverGroup, kStatusCompactMap);
    if (map.empty()) {
        return;
    }
    // A full list: 00, the stamp (01 sequence:size64), count:size, entries (statusIndex:u16 packed:u8).
    Reader reader(map);
    if (reader.size() != 0 || reader.u8() != 1) {
        return;
    }
    const std::uint64_t stamp = reader.size64();
    const std::uint32_t count = reader.size();
    if (!reader.ok() || count > reader.left() / 3) {
        return;
    }
    std::vector<Entity::StatusEntry> entries;
    for (std::uint32_t index = 0; index < count; ++index) {
        Entity::StatusEntry entry;
        entry.index = reader.u16();
        const std::uint8_t packed = reader.u8();
        entry.stacks = static_cast<std::uint8_t>(packed & 0x7F);
        entry.potencyOne = (packed & kPackedPotencyOne) != 0;
        entries.push_back(entry);
    }
    if (!reader.ok() || (entity.hasStatuses && stamp < entity.statusStamp)) {
        return;
    }
    entity.hasStatuses = true;
    entity.statusStamp = stamp;
    entity.statuses = std::move(entries);
    ++entity.statusRevision;
    m_changes.push_back({Change::Kind::status, entity.interest, entity.serial});
}

void EntityTable::apply_spell(const decoder::Fragment& fragment, Entity& entity, std::uint64_t nowUs) {
    SpellParts& spell = entity.spell;
    if (!entity.hasSpellParts) {
        spell.firstSeenUs = nowUs;
    }
    entity.hasSpellParts = true;
    if (fragment.type == kTypeSpell) {
        spell.hasSpell = true;
        if (const auto key = decoder::field_of(fragment, 0, kSpellKey); key.size() == 4) {
            spell.key = static_cast<std::uint32_t>(read_be(key));
        }
        if (const auto place = decoder::field_of(fragment, 0, kSpellPlace); place.size() == 10) {
            spell.place = wide_position(place);
        }
        if (const auto rotation = decoder::field_of(fragment, 0, kSpellRotation); !rotation.empty()) {
            spell.rotation = swc::net::read_quaternion(rotation);
        }
        if (const auto attachment = decoder::field_of(fragment, 0, kSpellAttachment); attachment.size() == 24) {
            // No entity: the zero actor id and the group 00000000FFFFFFFF.
            std::copy(attachment.begin(), attachment.begin() + 16, spell.attachedActor.begin());
            spell.attachedGroup = read_be(attachment.subspan(16, 8));
            spell.attached = spell.attachedGroup != 0x00000000FFFFFFFFULL
                             && std::any_of(spell.attachedActor.begin(), spell.attachedActor.end(), [](std::uint8_t byte) { return byte != 0; });
        }
        if (const auto caster = decoder::field_of(fragment, 0, kSpellCaster); caster.size() == 24) {
            std::copy(caster.begin(), caster.begin() + 16, spell.casterActor.begin());
            spell.caster = read_be(caster.subspan(16, 8));
        }
        if (const auto spawn = decoder::field_of(fragment, 0, kSpellSpawn); spawn.size() == 8) {
            spell.spawnUs = read_be(spawn);
        }
        if (const auto scale = decoder::field_of(fragment, 0, kSpellRadiusScale); scale.size() == 4) {
            spell.radiusScale = f32_at(scale);
        }
    } else {
        // A delayed event: the time that is left from now (a half float, s) and the completed mark; a state with only one of
        // them keeps the other one.
        DelayedEventState& event = spell.events[fragment.slot];
        if (const auto remaining = decoder::field_of(fragment, 0, 0); remaining.size() == 2) {
            event.remaining = half_to_float(static_cast<std::uint16_t>(read_be(remaining)));
        }
        if (const auto completed = decoder::field_of(fragment, 0, 1); completed.size() == 1) {
            event.completed = completed[0] != 0;
        }
        event.appliedUs = nowUs;
    }
    m_changes.push_back({Change::Kind::effect, entity.interest, entity.serial});
}

void EntityTable::apply_movement(const decoder::Fragment& fragment, Entity& entity) {
    ++entity.movementStates;
    // The client ignores a movement state with no time anchor (protocol notes 4.4).
    const auto anchor = decoder::field_of(fragment, 0, kTimeAnchor);
    if (anchor.size() != 8) {
        return;
    }
    const std::uint64_t timeUs = read_be(anchor);
    if (const auto bits = decoder::field_of(fragment, 0, kDataBits); bits.size() == 1) {
        entity.dataBits = bits[0];
    }
    if (const auto teleport = decoder::field_of(fragment, 0, kTeleport); teleport.size() == 1) {
        entity.teleport = teleport[0];
    }
    const auto position = decoder::field_of(fragment, 0, kWorldPosition);
    const auto rotation = decoder::field_of(fragment, 0, kRotation);
    if (position.size() == 10 || !rotation.empty()) {
        MovementSample sample;
        sample.timeUs = timeUs;
        if (!entity.samples.empty()) {
            sample.position = entity.samples.back().position;
            sample.rotation = entity.samples.back().rotation;
        }
        if (position.size() == 10) {
            sample.position = swc::net::read_world_position(position);
        }
        if (!rotation.empty()) {
            sample.rotation = swc::net::read_quaternion(rotation);
        }
        // A state with the time of the newest sample replaces it (two parts of one tick).
        if (!entity.samples.empty() && entity.samples.back().timeUs >= timeUs) {
            entity.samples.back() = sample;
        } else {
            entity.samples.push_back(sample);
        }
        while (entity.samples.size() > kSamples) {
            entity.samples.pop_front();
        }
    }
    const std::uint32_t before = entity.scopeRevision;
    const auto previous = entity.scope(0);
    const std::uint16_t previousStart = previous != nullptr ? previous->startCount : 0;
    const bool hadAction = previous != nullptr;
    if (read_scopes(fragment, entity)) {
        entity.scopeTimeUs = timeUs;
        ++entity.scopeRevision;
        const auto now = entity.scope(0);
        if (before == 0 || hadAction != (now != nullptr) || (now != nullptr && now->startCount != previousStart)) {
            m_changes.push_back({Change::Kind::action, entity.interest, entity.serial});
        }
    }
}

bool EntityTable::read_scopes(const decoder::Fragment& fragment, Entity& entity) {
    // The client reads the scope fields only when the state has scopeTimeBlobEx (protocol notes 4.14).
    const auto timeBlob = decoder::field_of(fragment, 0, kScopeTimeBlob);
    const auto maskValue = decoder::field_of(fragment, 0, kScopeData);
    const auto infoBlob = decoder::field_of(fragment, 0, kScopeInfoBlob);
    if (timeBlob.empty() || maskValue.empty() || infoBlob.empty()) {
        return false;
    }
    const std::uint32_t mask = size_of(maskValue);
    // The records, one for each bit of the mask in the order of the scopes: start count:u16, fragment + 1, action + 1
    // (size codes), the playback scale (half float), the option:u8 (client reader 0x2A75F70).
    Reader records(infoBlob);
    const std::uint32_t recordBytes = records.size();
    const std::size_t recordStart = records.position();
    std::map<std::uint32_t, ScopeRecord> scopes;
    for (std::uint32_t scope = 0; scope < 32 && records.ok(); ++scope) {
        if ((mask & (1U << scope)) == 0) {
            continue;
        }
        ScopeRecord record;
        record.startCount = records.u16();
        const std::uint32_t fragmentPlusOne = records.size();
        const std::uint32_t actionPlusOne = records.size();
        record.fragment = fragmentPlusOne == 0 ? EntityTable::kNone : fragmentPlusOne - 1;
        record.action = actionPlusOne == 0 ? EntityTable::kNone : actionPlusOne - 1;
        record.speed = half_to_float(records.u16());
        record.option = records.u8();
        scopes[scope] = record;
    }
    if (!records.ok() || records.position() - recordStart > recordBytes) {
        return false;
    }
    // The times: scope 0 in four bytes of ticks (low byte first); each scope of 2 or more a big-endian half float in seconds
    // (0x14087AE90); a record with no time stops the read of the later scopes.
    std::uint32_t ticks = 0;
    const unsigned timeFields[4] = {kScopeTimeLow, kScopeTimeByte1, kScopeTimeByte2, kScopeTimeByte3};
    for (unsigned index = 0; index < 4; ++index) {
        if (const auto value = decoder::field_of(fragment, 0, timeFields[index]); value.size() == 1) {
            ticks |= static_cast<std::uint32_t>(value[0]) << (8 * index);
        }
    }
    if (auto found = scopes.find(0); found != scopes.end()) {
        found->second.time = static_cast<float>(ticks) / EntityTable::kTicksPerSecond;
    }
    Reader times(timeBlob);
    const std::uint32_t timeBytes = times.size();
    std::uint32_t read = 0;
    for (auto& [scope, record] : scopes) {
        if (scope < 2) {
            continue;
        }
        if (read + 2 > timeBytes) {
            break;
        }
        record.time = half_to_float(times.u16());
        read += 2;
    }
    if (const auto tags = decoder::field_of(fragment, 0, kGlobalFragTags); tags.size() == entity.tags.size()) {
        std::copy(tags.begin(), tags.end(), entity.tags.begin());
    }
    entity.hasScopes = true;
    entity.scopeMask = mask;
    entity.scopes = std::move(scopes);
    return true;
}

void EntityTable::apply_vitals(const decoder::Fragment& fragment, Entity& entity) {
    ++entity.vitalsStates;
    const bool wasDead = entity.dead;
    const bool hadVitals = entity.hasVitals;
    if (const auto value = decoder::field_of(fragment, 0, kHealthAmount); value.size() == 4) {
        entity.health = f32_at(value);
        entity.hasVitals = true;
    }
    if (const auto value = decoder::field_of(fragment, 0, kHealthMax); value.size() == 4) {
        entity.healthMax = f32_at(value);
    }
    if (const auto value = decoder::field_of(fragment, 0, kVitalsData); !value.empty()) {
        entity.dead = (value[0] & 0x01) != 0;
    }
    if (const auto value = decoder::field_of(fragment, 0, kVitalsId); value.size() == 4) {
        entity.vitalsId = static_cast<std::uint32_t>(read_be(value));
    }
    if (const auto value = decoder::field_of(fragment, 0, kVitalsCategoryId); value.size() == 4) {
        entity.categoryId = static_cast<std::uint32_t>(read_be(value));
    }
    if (const auto value = decoder::field_of(fragment, 0, kVitalsLevel); value.size() == 4) {
        entity.level = static_cast<std::uint32_t>(read_be(value));
    }
    if (const auto value = decoder::field_of(fragment, 0, kInvulnerability); value.size() == 1) {
        entity.immune = value[0] != 0;
    }
    // A health change reaches the name plate only with bit 0 of healthChangeFlags (client test 0x689FDC0).
    if (const auto value = decoder::field_of(fragment, 0, kHealthChangeFlags); value.size() == 1 && (value[0] & 0x01) != 0) {
        ++entity.healthRevision;
        if (hadVitals) {
            m_changes.push_back({Change::Kind::health, entity.interest, entity.serial});
        }
    }
    if (entity.dead && !wasDead) {
        m_changes.push_back({Change::Kind::death, entity.interest, entity.serial});
    }
}

} // namespace swc::creatures
