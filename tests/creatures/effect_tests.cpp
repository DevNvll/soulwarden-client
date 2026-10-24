// Tests of the shots and the danger areas of the creatures (stage P2.9; Creatures/SwcEntityTable, Creatures/SwcEffects) with the
// states that the encoders of the backend make: the Projectile state (server/rep/states/projectile_state.cpp), the Spell state,
// the PositionInTheWorld state, and the DelayedEvent states (spell_state.cpp, object_states.cpp). tools/test-creatures.sh builds
// it with the system compiler; the Soulwarden sources are read only. The slices have the values of the game data
// (basic_ai_arrow; undead_shaman_lightning_strike; mage_ranged_aoe), and the loot bag (stage P2.10: metadata, PositionInTheWorld,
// Interact, LootDrop; object_states.cpp, interaction_states.cpp, loot_bag_states.cpp).

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

#include "Creatures/SwcEffects.h"
#include "Creatures/SwcEntityTable.h"
#include "Creatures/SwcMeleeContact.h"
#include "gamedata/creature_data.h"
#include "gamedata/spell_data.h"
#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "support/state_decoder.h"
#include "world/entity.h"
#include "world/world_clock.h"

// The Spell state links the id of a vitals row (a spell with health; none here), and the file of the LootDrop state links the item
// encoder of the GlobalStorage state (not used here).
namespace soulwarden::backend::gamedata {
std::uint32_t progression_id(std::string_view) {
    return 0;
}
} // namespace soulwarden::backend::gamedata
namespace soulwarden::backend::server::rep {
hub::Bytes encode_item(const world::ItemInstance*, std::uint32_t, std::uint8_t, ItemWire) {
    return {};
}
} // namespace soulwarden::backend::server::rep

namespace {

namespace backend = soulwarden::backend;
namespace decoder = soulwarden::backend::tests;
namespace rep = soulwarden::backend::server::rep;
using swc::creatures::AreaPhase;
using swc::creatures::EntityTable;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

class TestClock final : public backend::world::WorldClock {
public:
    [[nodiscard]] time_point now() const override { return m_now; }
    void advance(std::chrono::milliseconds step) { m_now += step; }

protected:
    [[nodiscard]] std::int64_t wall_offset_ns() const override { return 0; }

private:
    time_point m_now{std::chrono::seconds(1000)};
};

std::uint64_t server_us(const TestClock& clock) {
    return clock.server_time_ns() / 1000;
}

std::uint64_t world_us(const TestClock& clock) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(clock.now().time_since_epoch()).count());
}

decoder::Fragment fragment_of(std::uint32_t slot, std::uint32_t type, const backend::server::hub::Bytes& state, const char* name) {
    decoder::Fragment fragment;
    fragment.slot = slot;
    fragment.type = type;
    std::size_t position = 0;
    const bool read = decoder::detail::read_state(state, position, fragment);
    check(read && position == state.size(), std::string(name) + ": the state reader reads the state of the encoder to its end");
    return fragment;
}

template <std::size_t N>
void set_asset(std::array<std::uint8_t, 16>& out, const std::uint8_t (&bytes)[N]) {
    std::copy(std::begin(bytes), std::end(bytes), out.begin());
}

// --------------------------------------------------------------------------------------------------------------- the shot

struct Shot {
    backend::gamedata::ProjectileSliceData slice;
    backend::world::Entity entity;
};

/** basic_ai_arrow (6158A382-90E5-5F05-BA91-4FD8991C1C5C:2): slot 1, m_accel (0, 0, -2), m_flyingLifetime 1.5, m_stuckLifetime 8. */
void make_arrow(Shot& shot, const TestClock& clock) {
    const std::uint8_t asset[16] = {0x61, 0x58, 0xA3, 0x82, 0x90, 0xE5, 0x5F, 0x05, 0xBA, 0x91, 0x4F, 0xD8, 0x99, 0x1C, 0x1C, 0x5C};
    set_asset(shot.slice.asset, asset);
    shot.slice.subId = 2;
    shot.slice.slot = 1;
    shot.slice.accel = {0.0F, 0.0F, -2.0F};
    shot.entity.kind = backend::world::EntityKind::projectile;
    for (std::size_t index = 0; index < 16; ++index) {
        shot.entity.actor.bytes[index] = static_cast<std::uint8_t>(0x70 + index);
    }
    shot.entity.shot.slice = &shot.slice;
    shot.entity.shot.source = 0x4C554F5340000064ULL;
    shot.entity.shot.attackIndex = 7;
    shot.entity.shot.ammoKey = 2258403498U;
    shot.entity.shot.tableKey = 2370114060U;
    shot.entity.shot.rowIndex = 3;
    shot.entity.shot.origin = {8880.5F, 4252.1F, 139.6F};
    shot.entity.shot.velocity = {0.0F, -49.9F, 1.2F};
    shot.entity.shot.launched = clock.now();
}

decoder::Record shot_record(Shot& shot, const TestClock& clock, std::uint32_t interest, backend::world::Revision since) {
    rep::PartsRequest request;
    request.since = since;
    request.clock = &clock;
    decoder::Record record;
    record.interest = interest;
    if (since == 0) {
        record.fragments.push_back(fragment_of(0, 10, rep::encode_metadata_state(shot.entity, rep::View::observer, clock), "metadata (shot)"));
    }
    for (const auto& part : rep::encode_projectile_parts(shot.entity, rep::View::observer, request)) {
        record.fragments.push_back(fragment_of(shot.slice.slot, 16, part, "projectile"));
    }
    return record;
}

void test_shot() {
    TestClock clock;
    Shot shot;
    make_arrow(shot, clock);
    EntityTable table;
    table.apply(shot_record(shot, clock, 40, 0), server_us(clock));
    const swc::creatures::Entity* arrow = table.find(40);
    check(arrow != nullptr && arrow->hasMetadata && arrow->slice_key() == "6158A382-90E5-5F05-BA91-4FD8991C1C5C:2",
          "the metadata of the shot: the key of basic_ai_arrow");
    check(arrow != nullptr && arrow->hasProjectile && arrow->projectile.hasLaunch && !arrow->projectile.collided, "the launch state");
    if (arrow == nullptr) {
        return;
    }
    const swc::creatures::Projectile& launch = arrow->projectile;
    check(near(launch.anchor.x, 8880.5, 1e-3) && near(launch.anchor.y, 4252.1, 1e-3) && near(launch.anchor.z, 139.6, 0.04),
          "the anchor (the height is a u16 over -100 to 2000 m)");
    check(near(launch.velocity[0], 0.0, 1e-3) && near(launch.velocity[1], -49.9, 0.05) && near(launch.velocity[2], 1.2, 0.002),
          "the velocity (three half floats)");
    check(launch.launchUs == world_us(clock), "the time anchor: the world clock in microseconds");
    check(launch.owner == 0x4C554F5340000064ULL && launch.ammoKey == 2258403498U, "the owner and the ammunition key");
    bool changed = false;
    for (const auto& change : table.take_changes()) {
        changed = changed || change.kind == swc::creatures::Change::Kind::effect;
    }
    check(changed, "a change of the kind effect");

    // The flight of the client: the anchor, the velocity, and the acceleration of the slice.
    const swc::creatures::ProjectileFlight flight{{0.0F, 0.0F, -2.0F}, 1.5F, 8.0F};
    bool held = true;
    const auto half = swc::creatures::projectile_place(launch, flight, launch.launchUs + 500000, &held);
    check(!held && near(half.y, launch.anchor.y + launch.velocity[1] * 0.5, 1e-3)
              && near(half.z, launch.anchor.z + launch.velocity[2] * 0.5 - 0.25, 1e-3),
          "the place 0.5 s after the launch: anchor + v t + a t^2 / 2");
    const auto before = swc::creatures::projectile_place(launch, flight, launch.launchUs - 100000);
    check(near(before.y, launch.anchor.y, 1e-6), "no flight before the time anchor");
    const auto speed = swc::creatures::projectile_velocity(launch, flight, launch.launchUs + 1000000);
    check(near(speed[2], launch.velocity[2] - 2.0, 1e-3), "the velocity after 1 s: the acceleration of the slice");

    // The contact: the collision state holds the shot at the contact place.
    clock.advance(std::chrono::milliseconds(400));
    shot.entity.shot.contact = backend::state::WorldPosition{8880.5F, 4232.2F, 139.4F};
    shot.entity.shot.contactAt = clock.now();
    table.apply(shot_record(shot, clock, 40, 5), server_us(clock));
    arrow = table.find(40);
    check(arrow != nullptr && arrow->projectile.collided && arrow->projectile.contactUs == world_us(clock), "the collision state");
    if (arrow != nullptr) {
        check(near(arrow->projectile.contact.y, 4232.2, 1e-3) && near(arrow->projectile.launchUs, launch.launchUs, 0.5),
              "the contact place; the launch stays");
        const auto stuck = swc::creatures::projectile_place(arrow->projectile, flight, arrow->projectile.contactUs + 3000000, &held);
        check(held && near(stuck.y, 4232.2, 1e-3), "after the contact the shot holds at the contact place");
    }

    // A session that sees the shot after the contact: one record with the launch and the collision.
    table.apply(shot_record(shot, clock, 41, 0), server_us(clock));
    const swc::creatures::Entity* late = table.find(41);
    check(late != nullptr && late->projectile.hasLaunch && late->projectile.collided, "a first record after the contact: the launch and the collision");

    // The stop of the record removes the shot.
    table.take_changes();
    table.stop(40);
    bool removed = false;
    for (const auto& change : table.take_changes()) {
        removed = removed || (change.kind == swc::creatures::Change::Kind::removed && change.interest == 40);
    }
    check(table.find(40) == nullptr && removed, "the stop removes the shot");
}

// --------------------------------------------------------------------------------------------------------------- the areas

struct Area {
    backend::gamedata::SpellSlice slice;
    backend::world::Entity entity;
    std::vector<swc::creatures::SpellEventData> events;
    swc::creatures::SpellVolumeData volume;
};

void add_event(Area& area, std::uint32_t slot, float delay, std::uint32_t type, const char* target) {
    backend::gamedata::SpellEvent& event = area.slice.events.emplace_back();
    event.slot = slot;
    event.delay = delay;
    event.type = type;
    event.target = target;
    area.events.push_back({slot, delay, type, target});
}

/** undead_shaman_lightning_strike (66BE4F91-CE70-5C93-A219-333ACD03770A:2): the Spell state at slot 1, five delayed events, the
 *  volume Damage (no shape: the row, Radius 2.5) from 0.9 s to 0.95 s; the life 5 s. */
void make_strike(Area& area, const TestClock& clock) {
    const std::uint8_t asset[16] = {0x66, 0xBE, 0x4F, 0x91, 0xCE, 0x70, 0x5C, 0x93, 0xA2, 0x19, 0x33, 0x3A, 0xCD, 0x03, 0x77, 0x0A};
    set_asset(area.slice.asset, asset);
    area.slice.subId = 2;
    area.slice.spellSlot = 1;
    add_event(area, 3, 0.65F, 2, "Audio_Undead_Shaman_Lightning_Strike");
    add_event(area, 4, 0.9F, 2, "Damage");
    add_event(area, 5, 0.95F, 3, "Damage");
    add_event(area, 6, 0.85F, 3, "VFXArea");
    add_event(area, 7, 0.9F, 2, "VFXDamage");
    area.volume = {"Damage", "none", 0.0F, 0.0F, 0.0F, 0.0F, 0.9F, 0.95F};
    area.entity.kind = backend::world::EntityKind::spell;
    for (std::size_t index = 0; index < 16; ++index) {
        area.entity.actor.bytes[index] = static_cast<std::uint8_t>(0x90 + index);
    }
    area.entity.position = {8884.0F, 4251.0F, 138.3F};
    area.entity.spell.slice = &area.slice;
    area.entity.spell.spellId = "Undead_Shaman_Lightning_Strike";
    area.entity.spell.key = 0x5D1A2B3CU;
    area.entity.spell.caster = 0x4C554F5340000068ULL;
    for (std::size_t index = 0; index < 16; ++index) {
        area.entity.spell.casterActor.bytes[index] = static_cast<std::uint8_t>(0x50 + index);
    }
    area.entity.spell.started = clock.now();
    area.entity.spell.ends = clock.now() + std::chrono::seconds(5);
}

decoder::Record area_record(Area& area, const TestClock& clock, std::uint32_t interest) {
    rep::PartsRequest request;
    request.since = 0;
    request.clock = &clock;
    decoder::Record record;
    record.interest = interest;
    record.fragments.push_back(fragment_of(0, 10, rep::encode_metadata_state(area.entity, rep::View::observer, clock), "metadata (spell)"));
    if (area.slice.spellSlot != 0) {
        record.fragments.push_back(fragment_of(area.slice.spellSlot, 2912, rep::encode_spell_state(area.entity, rep::View::observer, clock), "spell"));
    }
    if (area.slice.positionSlot != 0) {
        record.fragments.push_back(fragment_of(area.slice.positionSlot, 13, rep::encode_spell_place_state(area.entity, rep::View::observer, clock),
                                               "position in the world"));
    }
    for (const rep::SlotState& state : rep::encode_delayed_event_states(area.entity, rep::View::observer, request)) {
        record.fragments.push_back(fragment_of(state.slot, 3139, state.state, "delayed event"));
    }
    return record;
}

void test_strike() {
    TestClock clock;
    Area area;
    make_strike(area, clock);
    EntityTable table;
    table.apply(area_record(area, clock, 50), server_us(clock));
    const swc::creatures::Entity* strike = table.find(50);
    check(strike != nullptr && strike->slice_key() == "66BE4F91-CE70-5C93-A219-333ACD03770A:2", "the metadata of the strike");
    check(strike != nullptr && strike->hasSpellParts && strike->spell.hasSpell, "the Spell state");
    if (strike == nullptr) {
        return;
    }
    const swc::creatures::SpellParts& parts = strike->spell;
    check(parts.key == 0x5D1A2B3CU && parts.spawnUs == world_us(clock), "the key and the spawn time");
    check(near(parts.place.x, 8884.0, 1e-3) && near(parts.place.y, 4251.0, 1e-3) && near(parts.place.z, 138.3, 0.04), "the start place");
    check(parts.caster == swc::net::group_of(parts.casterActor) && parts.casterActor[0] == 0x50, "the caster reference");
    check(near(parts.radiusScale, 1.0, 1e-6), "the radius scale");
    check(parts.events.size() == 5, "a state for each of the five delayed events");

    const swc::creatures::AreaTimes times = swc::creatures::area_times(parts, area.volume, area.events);
    const std::uint64_t start = parts.spawnUs;
    // The states of the delayed events have half floats: 1 ms here.
    check(times.startUs == start && near(static_cast<double>(times.dangerUs), static_cast<double>(start + 900000), 2000.0),
          "the danger starts at the activation of Damage (0.9 s)");
    check(near(static_cast<double>(times.endUs), static_cast<double>(start + 1200000), 2000.0), "the danger ends at 0.95 s, shown at least 0.3 s (1.2 s)");
    check(swc::creatures::area_phase(times, start + 500000) == AreaPhase::warning, "0.5 s: the warning");
    check(swc::creatures::area_phase(times, start + 1000000) == AreaPhase::danger, "1.0 s: the danger");
    check(swc::creatures::area_phase(times, start + 1300000) == AreaPhase::ended, "1.3 s: the end");

    // A session that sees the strike 2 s after its start: the activations passed and are held (the time to the end of the spell and
    // 1 s), the deactivations are completed: no warning and no bolt.
    clock.advance(std::chrono::milliseconds(2000));
    EntityTable late;
    late.apply(area_record(area, clock, 51), server_us(clock));
    const swc::creatures::Entity* seen = late.find(51);
    check(seen != nullptr, "the late record of the strike");
    if (seen != nullptr) {
        const auto held = seen->spell.events.find(4);
        check(held != seen->spell.events.end() && !held->second.completed && near(held->second.remaining, 4.0, 0.01),
              "a passed activation is held: 3 s to the end of the spell and 1 s");
        const auto lateTimes = swc::creatures::area_times(seen->spell, area.volume, area.events);
        check(swc::creatures::area_phase(lateTimes, server_us(clock)) == AreaPhase::ended, "the late session shows no danger area");
    }
}

/** mage_ranged_aoe (F4BB3386-DA69-5D4F-8B3B-2E8167C6B061:2): no Spell state, PositionInTheWorld at slot 1, the activation of
 *  Arcane_Anomaly at 1.0 s (slot 2) and Destroy at 26 s (slot 3); the volume is a sphere of 2.5 m from 1.0 s. */
void test_status_area() {
    TestClock clock;
    Area area;
    const std::uint8_t asset[16] = {0xF4, 0xBB, 0x33, 0x86, 0xDA, 0x69, 0x5D, 0x4F, 0x8B, 0x3B, 0x2E, 0x81, 0x67, 0xC6, 0xB0, 0x61};
    set_asset(area.slice.asset, asset);
    area.slice.subId = 2;
    area.slice.positionSlot = 1;
    add_event(area, 2, 1.0F, 2, "Arcane_Anomaly");
    add_event(area, 3, 26.0F, 0, "Destroy");
    area.volume = {"Arcane_Anomaly", "sphere", 2.5F, 2.5F, 0.0F, 0.25F, 1.0F, -1.0F};
    area.entity.kind = backend::world::EntityKind::spell;
    area.entity.position = {8878.0F, 4255.0F, 138.2F};
    area.entity.spell.slice = &area.slice;
    area.entity.spell.started = clock.now();
    area.entity.spell.ends = clock.now() + std::chrono::seconds(26);
    // The session sees the area 0.4 s after its start.
    clock.advance(std::chrono::milliseconds(400));
    EntityTable table;
    table.apply(area_record(area, clock, 60), server_us(clock));
    const swc::creatures::Entity* cloud = table.find(60);
    check(cloud != nullptr && cloud->hasSpellParts && !cloud->spell.hasSpell && cloud->hasWorldPlace, "a status area: no Spell state, the place");
    if (cloud == nullptr) {
        return;
    }
    check(near(cloud->worldPlace.x, 8878.0, 1e-3) && near(cloud->worldPlace.y, 4255.0, 1e-3), "PositionInTheWorld");
    const auto times = swc::creatures::area_times(cloud->spell, area.volume, area.events);
    const std::uint64_t start = server_us(clock) - 400000;
    check(near(static_cast<double>(times.startUs), static_cast<double>(start), 2000.0),
          "the start from the state of the first delayed event (the time that is left, less its delay)");
    check(near(static_cast<double>(times.dangerUs), static_cast<double>(start + 1000000), 2000.0) && times.endUs == 0,
          "the danger from 1.0 s to the stop of the record");
    check(swc::creatures::area_phase(times, start + 900000) == AreaPhase::warning
              && swc::creatures::area_phase(times, start + 20000000) == AreaPhase::danger,
          "the warning before 1.0 s, the danger after it");
    table.stop(60);
    check(table.find(60) == nullptr, "the stop removes the area");
}

/** The vortex of Gravity Well (greataxe_gravitywell_vortex, D60BAED0-C08F-514F-9ECD-9BB5E20F7E9D:2; stage P3.5): the Spell state at
 *  slot 1, the events of slots 4 to 9 (the burst at 3.0 s, the root off at 1.0 s, Destroy at 3.5 s), the volume DetectionVolume (a
 *  sphere of 4 m from 0 s to 3.1 s). A spell of a character: its caster is no creature. */
void test_vortex() {
    TestClock clock;
    Area area;
    const std::uint8_t asset[16] = {0xD6, 0x0B, 0xAE, 0xD0, 0xC0, 0x8F, 0x51, 0x4F, 0x9E, 0xCD, 0x9B, 0xB5, 0xE2, 0x0F, 0x7E, 0x9D};
    set_asset(area.slice.asset, asset);
    area.slice.subId = 2;
    area.slice.spellSlot = 1;
    add_event(area, 4, 3.0F, 3, "VFX_Foe");
    add_event(area, 5, 3.0F, 3, "VFX_Vortex");
    add_event(area, 6, 3.1F, 3, "DetectionVolume");
    add_event(area, 7, 3.0F, 2, "Burst");
    add_event(area, 8, 1.0F, 3, "VortexRoot");
    add_event(area, 9, 3.5F, 0, "Destroy");
    area.volume = {"DetectionVolume", "sphere", 4.0F, 4.0F, 0.0F, 0.0F, 0.0F, 3.1F};
    area.entity.kind = backend::world::EntityKind::spell;
    for (std::size_t index = 0; index < 16; ++index) {
        area.entity.actor.bytes[index] = static_cast<std::uint8_t>(0xB0 + index);
    }
    area.entity.position = {8884.0F, 4250.0F, 138.2F};
    area.entity.spell.slice = &area.slice;
    area.entity.spell.spellId = "Spell_GreatAxe_GravityWell_Vortex";
    area.entity.spell.key = 0x11223344U;
    // The caster: a character (the reference of the Spell state is its actor id and the G of that actor id).
    const std::uint8_t caster[16] = {0x02, 0x00, 0x00, 0x00, 0x53, 0x4F, 0x55, 0x4C, 0x10, 0x00, 0x00, 0x00, 0x53, 0x4F, 0x55, 0x4C};
    std::copy(std::begin(caster), std::end(caster), area.entity.spell.casterActor.bytes.begin());
    area.entity.spell.caster = backend::world::entity_group_id(area.entity.spell.casterActor);
    area.entity.spell.started = clock.now();
    area.entity.spell.ends = clock.now() + std::chrono::milliseconds(3500);
    // The direction of the caster (stage 3.4f-D): strtrot has the place rotation format (u16 for each axis, AST-275).
    area.entity.spell.yaw = 164.0F;
    EntityTable table;
    table.apply(area_record(area, clock, 80), server_us(clock));
    const swc::creatures::Entity* vortex = table.find(80);
    check(vortex != nullptr && vortex->slice_key() == "D60BAED0-C08F-514F-9ECD-9BB5E20F7E9D:2" && vortex->spell.hasSpell, "the record of the vortex");
    if (vortex == nullptr) {
        return;
    }
    check(vortex->spell.caster == 0x4C554F5300000002ULL && vortex->spell.casterActor[0] == 0x02 && !vortex->spell.attached,
          "the caster (a character: its actor id and G); no attachment");
    check(vortex->spell.events.size() == 6, "a state for each of the six delayed events");
    const double half = 164.0 * 3.14159265358979 / 360.0;
    check(near(vortex->spell.rotation.x, 0.0, 1e-4) && near(vortex->spell.rotation.y, 0.0, 1e-4)
              && near(vortex->spell.rotation.z, std::sin(half), 1e-4) && near(vortex->spell.rotation.w, std::cos(half), 1e-4),
          "the start rotation of the Spell state (strtrot): the yaw of the caster");
    const auto times = swc::creatures::area_times(vortex->spell, area.volume, area.events);
    const std::uint64_t start = vortex->spell.spawnUs;
    check(times.dangerUs == start && near(static_cast<double>(times.endUs), static_cast<double>(start + 3100000), 5000.0),
          "the vortex is active from its start to 3.1 s");
    check(swc::creatures::area_phase(times, start + 1000000) == AreaPhase::danger
              && swc::creatures::area_phase(times, start + 3300000) == AreaPhase::ended,
          "1.0 s: active (the pull); 3.3 s: gone (the burst at 3.0 s)");
    table.stop(80);
    check(table.find(80) == nullptr, "the stop removes the vortex (3.5 s)");
}

/** The Beacon of the life staff (spell_life_staff_beacon, 9A996354-A2B4-5D4E-BB56-684B27EF5E14:2; stage P3.5): field 6 of the Spell
 *  state is the entity that it follows (spells.cpp: the actor id and G of the entity). */
void test_beacon() {
    TestClock clock;
    Area area;
    const std::uint8_t asset[16] = {0x9A, 0x99, 0x63, 0x54, 0xA2, 0xB4, 0x5D, 0x4E, 0xBB, 0x56, 0x68, 0x4B, 0x27, 0xEF, 0x5E, 0x14};
    set_asset(area.slice.asset, asset);
    area.slice.subId = 2;
    area.slice.spellSlot = 1;
    add_event(area, 4, 3.0F, 2, "Destroy");
    add_event(area, 5, 0.05F, 2, "AreaTrigger");
    area.entity.kind = backend::world::EntityKind::spell;
    for (std::size_t index = 0; index < 16; ++index) {
        area.entity.actor.bytes[index] = static_cast<std::uint8_t>(0xC0 + index);
    }
    area.entity.position = {8880.0F, 4252.0F, 138.2F};
    area.entity.spell.slice = &area.slice;
    area.entity.spell.key = 0x55667788U;
    area.entity.spell.started = clock.now();
    area.entity.spell.ends = clock.now() + std::chrono::seconds(10);
    for (std::size_t index = 0; index < 16; ++index) {
        area.entity.spell.attachedActor.bytes[index] = static_cast<std::uint8_t>(0x40 + index);
    }
    area.entity.spell.attached = backend::world::entity_group_id(area.entity.spell.attachedActor);
    EntityTable table;
    table.apply(area_record(area, clock, 81), server_us(clock));
    const swc::creatures::Entity* beacon = table.find(81);
    check(beacon != nullptr && beacon->spell.attached, "the Beacon follows an entity (field 6)");
    if (beacon != nullptr) {
        check(beacon->spell.attachedGroup == area.entity.spell.attached && beacon->spell.attachedActor[0] == 0x40
                  && beacon->spell.attachedActor[15] == 0x4F,
              "the actor id and G of the followed entity");
    }
}

// ------------------------------------------------------------------------------------------------------------ the loot bag

/** The personal loot container (instanced_loot_container, 9844F2DE-C3B1-5BD3-98FA-8768B5445906:2; interaction-data.json template
 *  93): PositionInTheWorld at slot 1, Interact at slot 5 (component 2953d2fecfa764bd of the entity 000000dd28f454df; the root
 *  000000de28f454df), LootDrop at slot 6. */
void test_loot_bag() {
    TestClock clock;
    backend::world::Entity bag;
    bag.kind = backend::world::EntityKind::object;
    for (std::size_t index = 0; index < 16; ++index) {
        bag.actor.bytes[index] = static_cast<std::uint8_t>(0xA0 + index);
    }
    bag.id = backend::world::entity_group_id(bag.actor);
    const std::uint8_t asset[16] = {0x98, 0x44, 0xF2, 0xDE, 0xC3, 0xB1, 0x5B, 0xD3, 0x98, 0xFA, 0x87, 0x68, 0xB5, 0x44, 0x59, 0x06};
    set_asset(bag.object.asset, asset);
    bag.object.subId = 2;
    bag.object.positionSlot = 1;
    bag.object.lootDropSlot = 6;
    bag.object.rarity = 3;
    bag.object.bounds.push_back(backend::world::ObjectBound{5, true, 0});
    bag.position = {8876.4F, 4251.2F, 138.21F};
    rep::PartsRequest request;
    request.clock = &clock;
    decoder::Record record;
    record.interest = 70;
    record.fragments.push_back(fragment_of(0, 10, rep::encode_metadata_state(bag, rep::View::observer, clock), "metadata (bag)"));
    record.fragments.push_back(fragment_of(1, 13, rep::encode_object_place_state(bag, rep::View::observer, clock), "position in the world (bag)"));
    for (const rep::SlotState& state : rep::encode_interact_states(bag, rep::View::observer, request)) {
        record.fragments.push_back(fragment_of(state.slot, 2930, state.state, "interact (bag)"));
    }
    record.fragments.push_back(fragment_of(6, 2027, rep::encode_loot_drop_state(bag, rep::View::observer, clock), "loot drop"));
    EntityTable table;
    table.apply(record, server_us(clock));
    const swc::creatures::Entity* seen = table.find(70);
    check(seen != nullptr && seen->slice_key() == "9844F2DE-C3B1-5BD3-98FA-8768B5445906:2", "the metadata of the bag: the key of the loot container");
    if (seen == nullptr) {
        return;
    }
    check(seen->hasWorldPlace && near(seen->worldPlace.x, 8876.4, 1e-3) && near(seen->worldPlace.y, 4251.2, 1e-3)
              && near(seen->worldPlace.z, 138.21, 0.04),
          "the place of the bag (PositionInTheWorld)");
    check(seen->hasLootDrop && seen->lootRarity == 3, "the rarity of the LootDrop state");
    const auto interact = seen->interact.find(5);
    check(interact != seen->interact.end() && interact->second.enabled && interact->second.users == 0, "the Interact state of slot 5");
    check(!seen->hasSpellParts && !seen->hasProjectile, "a bag is not a spell and not a shot");
    // The facet target of the Interact bound: the rule of the backend for a server child (Interactions::spawn: k = root ^ G,
    // target = (entity ^ k) + component).
    const std::uint64_t root = 0x000000DE28F454DFULL;
    const std::uint64_t entity = 0x000000DD28F454DFULL;
    const std::uint64_t component = 0x2953D2FECFA764BDULL;
    const std::uint64_t group = swc::net::group_of(seen->actor);
    check(group == bag.id, "G of the bag: the first 8 bytes of its actor id");
    check(swc::creatures::facet_target(group, entity ^ root, component) == (entity ^ (root ^ bag.id)) + component,
          "the target of the Interact bound: (entity XOR root XOR G) + component");
    // The disabled bound (the prompt goes away) and the stop.
    bag.object.bounds[0].enabled = false;
    decoder::Record change;
    change.interest = 70;
    for (const rep::SlotState& state : rep::encode_interact_states(bag, rep::View::observer, request)) {
        change.fragments.push_back(fragment_of(state.slot, 2930, state.state, "interact (disabled)"));
    }
    table.apply(change, server_us(clock));
    seen = table.find(70);
    check(seen != nullptr && !seen->interact.at(5).enabled, "a disabled Interact state");
    table.stop(70);
    check(table.find(70) == nullptr, "the stop removes the bag");
}

// ------------------------------------------------------------------------------------------------ the rotation of a place

/** The rotation of the PositionInTheWorld state (AST-275): the carpentry station of Everfall (interaction-data.json spawned, a turn
 *  about the vertical axis only) and a tilted rotation with w < 0 (the backend sends the negative, w not negative). The entity table
 *  must give the same rotation, not the tilt of the reader of the movement state. */
void test_place_rotation() {
    TestClock clock;
    const std::array<std::array<float, 4>, 2> rotations{{{0.0F, 0.0F, 0.990268F, 0.139173F}, {0.2F, -0.4F, 0.1F, -0.888819F}}};
    for (std::size_t index = 0; index < rotations.size(); ++index) {
        backend::world::Entity station;
        station.kind = backend::world::EntityKind::object;
        station.object.positionSlot = 2;
        station.object.rotation = rotations[index];
        station.position = {9338.851F, 2699.9934F, 82.7804F};
        decoder::Record record;
        record.interest = static_cast<std::uint32_t>(80 + index);
        record.fragments.push_back(fragment_of(2, 13, rep::encode_object_place_state(station, rep::View::observer, clock),
                                               "position in the world (station)"));
        EntityTable table;
        table.apply(record, server_us(clock));
        const swc::creatures::Entity* seen = table.find(record.interest);
        check(seen != nullptr && seen->hasWorldPlace, "the place of the station");
        if (seen == nullptr) {
            continue;
        }
        const auto& q = seen->worldPlaceRotation;
        const auto& r = rotations[index];
        const double sign = r[3] < 0.0F ? -1.0 : 1.0;
        check(near(q.x, sign * r[0], 1e-4) && near(q.y, sign * r[1], 1e-4) && near(q.z, sign * r[2], 1e-4) && near(q.w, sign * r[3], 1e-4),
              index == 0 ? "the rotation of the place: a turn about the vertical axis" : "the rotation of the place: a tilt with w < 0");
    }
}

} // namespace

int main() {
    test_shot();
    test_strike();
    test_status_area();
    test_loot_bag();
    test_vortex();
    test_beacon();
    test_place_rotation();
    std::printf("effects: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
