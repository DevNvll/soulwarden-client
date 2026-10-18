// Tests of the entity table of the creatures (Creatures/SwcEntityTable) with the states that the encoders of the backend make
// (apps/backend/src/server/rep/states: metadata, movement, vitals, marker). tools/test-creatures.sh builds it with the system
// compiler; the Soulwarden sources are read only.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "Creatures/SwcEntityTable.h"
#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "support/state_decoder.h"
#include "world/entity.h"
#include "world/world_clock.h"

namespace {

namespace backend = soulwarden::backend;
namespace decoder = soulwarden::backend::tests;
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

/** A world clock that the test sets. */
class TestClock final : public backend::world::WorldClock {
public:
    [[nodiscard]] time_point now() const override { return m_now; }
    void set(time_point now) { m_now = now; }
    void advance(std::chrono::milliseconds step) { m_now += step; }

protected:
    [[nodiscard]] std::int64_t wall_offset_ns() const override { return 0; }

private:
    time_point m_now{std::chrono::seconds(1000)};
};

/** @return A fragment of the state reader of the backend tests from the bytes of an encoder; the reader must read each byte. */
decoder::Fragment fragment_of(std::uint32_t slot, std::uint32_t type, const backend::server::hub::Bytes& state, const char* name) {
    decoder::Fragment fragment;
    fragment.slot = slot;
    fragment.type = type;
    std::size_t position = 0;
    const bool read = decoder::detail::read_state(state, position, fragment);
    check(read && position == state.size(), std::string(name) + ": the state reader reads the state of the encoder to its end");
    return fragment;
}

struct Scene {
    backend::gamedata::CreatureSlice slice;
    backend::gamedata::CreatureKind kind;
    backend::world::Entity entity;
    TestClock clock;
};

/** The slots of the Grey Wolf (wolf_grey_5): movement 5, vitals 9, marker 15 (protocol notes 4.14). */
constexpr std::uint32_t kMovementSlot = 5;
constexpr std::uint32_t kVitalsSlot = 9;
constexpr std::uint32_t kMarkerSlot = 15;

void make_wolf(Scene& scene) {
    // The asset id of the slice in the byte order of its text (gamedata read_asset): D86346A3-99D5-5AA5-94D1-5F6E5A8D4A17.
    const std::uint8_t asset[16] = {0xD8, 0x63, 0x46, 0xA3, 0x99, 0xD5, 0x5A, 0xA5, 0x94, 0xD1, 0x5F, 0x6E, 0x5A, 0x8D, 0x4A, 0x17};
    std::copy(std::begin(asset), std::end(asset), scene.slice.asset.begin());
    scene.slice.subId = 2;
    scene.slice.tags = {0x24, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    scene.kind.vitalsId = 0x7EA199D7;
    scene.kind.categoryId = 0x844F97B6;
    scene.entity.kind = backend::world::EntityKind::creature;
    for (std::size_t index = 0; index < 16; ++index) {
        scene.entity.actor.bytes[index] = static_cast<std::uint8_t>(0x40 + index);
    }
    scene.entity.creature.kind = &scene.kind;
    scene.entity.creature.body = &scene.slice;
    scene.entity.creature.level = 18;
    scene.entity.creature.yaw = 90.0F;
    scene.entity.position = {8815.25F, 4319.13F, 140.11F};
    scene.entity.vitals.health = 1690.876F;
    scene.entity.vitals.healthMax = 1690.876F;
}

decoder::Record first_record(Scene& scene, std::uint32_t interest) {
    namespace rep = backend::server::rep;
    rep::PartsRequest request;
    request.since = 0;
    request.clock = &scene.clock;
    decoder::Record record;
    record.interest = interest;
    record.fragments.push_back(fragment_of(0, 10, rep::encode_metadata_state(scene.entity, rep::View::observer, scene.clock), "metadata"));
    record.fragments.push_back(fragment_of(kMovementSlot, 11,
                                           rep::encode_creature_movement_state(scene.entity, scene.clock, 0, rep::kScopeUnknown, false),
                                           "movement (first)"));
    record.fragments.push_back(fragment_of(kVitalsSlot, 15, rep::encode_creature_vitals_state(scene.entity, rep::View::observer, request)[0],
                                           "vitals (first)"));
    record.fragments.push_back(fragment_of(kMarkerSlot, 5399, rep::encode_marker_state(scene.entity, rep::View::observer, scene.clock),
                                           "marker"));
    return record;
}

decoder::Record movement_record(Scene& scene, std::uint32_t interest, std::uint32_t sent, bool keepScope) {
    decoder::Record record;
    record.interest = interest;
    record.fragments.push_back(fragment_of(kMovementSlot, 11,
                                           backend::server::rep::encode_creature_movement_state(scene.entity, scene.clock, 7, sent, keepScope),
                                           "movement"));
    return record;
}

decoder::Record vitals_record(Scene& scene, std::uint32_t interest) {
    backend::server::rep::PartsRequest request;
    request.since = 7;
    request.clock = &scene.clock;
    decoder::Record record;
    record.interest = interest;
    record.fragments.push_back(fragment_of(kVitalsSlot, 15,
                                           backend::server::rep::encode_creature_vitals_state(scene.entity, backend::server::rep::View::observer,
                                                                                              request)[0],
                                           "vitals"));
    return record;
}

std::uint64_t server_us(const TestClock& clock) {
    return clock.server_time_ns() / 1000;
}

void test_first_record() {
    Scene scene;
    make_wolf(scene);
    EntityTable table;
    table.apply(first_record(scene, 2));
    const swc::creatures::Entity* wolf = table.find(2);
    check(wolf != nullptr, "the first record makes the entity of interest 2");
    if (wolf == nullptr) {
        return;
    }
    check(wolf->hasMetadata && wolf->slice_key() == "D86346A3-99D5-5AA5-94D1-5F6E5A8D4A17:2", "the slice key is the asset text of creatures.json: " + wolf->slice_key());
    check(wolf->actor[0] == 0x40 && wolf->actor[15] == 0x4F, "the actor id of the metadata state");
    check(wolf->movementSlot == kMovementSlot && wolf->samples.size() == 1, "one movement sample at the slot of the action list");
    const auto& sample = wolf->samples.back();
    check(near(sample.position.x, 8815.25, 1e-3) && near(sample.position.y, 4319.13, 1e-3) && near(sample.position.z, 140.11, 0.02),
          "worldPosAbs: the exact position (the height in 16 bits of 1,100 m)");
    // The yaw quaternion (0, 0, sin(a/2), cos(a/2)) of 90 degrees: a quarter turn to the west (protocol notes 4.14).
    check(near(sample.rotation.z, std::sin(M_PI / 4.0), 0.01) && near(sample.rotation.w, std::cos(M_PI / 4.0), 0.01), "rotation: yaw 90 degrees");
    check(sample.timeUs == server_us(scene.clock), "the time anchor is the server time in microseconds");
    check(wolf->hasScopes && wolf->scopeMask == 0 && wolf->scope(0) == nullptr, "the first record of a creature with no action: the scope fields with no record");
    check(wolf->tags == scene.slice.tags, "globalFragTags: the tags of the slice");
    check(wolf->dataBits == 0x03, "dataBits: the two target bits");
    check(wolf->hasVitals && near(wolf->health, 1690.876, 1e-3) && near(wolf->healthMax, 1690.876, 1e-3), "the health and its maximum");
    check(wolf->vitalsId == 0x7EA199D7 && wolf->categoryId == 0x844F97B6 && wolf->level == 18, "vitalsId, vitalsCategoryId, vitalsLevel");
    check(!wolf->dead && !wolf->immune, "not dead, not immune");
    check(wolf->hasMarker && wolf->markerEnabled, "the marker state: enabled");
    const auto changes = table.take_changes();
    check(!changes.empty() && changes.front().kind == swc::creatures::Change::Kind::added, "a change: the entity is new");
}

void test_actions() {
    Scene scene;
    make_wolf(scene);
    EntityTable table;
    table.apply(first_record(scene, 3));
    table.take_changes();
    // The bite of the wolf (action 8, fragment 5; protocol notes 4.14), 0.5 s after its start, with an end.
    backend::world::CreatureAction bite;
    bite.action = 8;
    bite.fragment = 5;
    bite.generation = 1;
    bite.tags = scene.slice.tags;
    bite.started = scene.clock.now() - std::chrono::milliseconds(500);
    bite.ends = bite.started + std::chrono::milliseconds(1433);
    scene.entity.creature.action = bite;
    table.apply(movement_record(scene, 3, backend::server::rep::kScopeNone, false));
    const swc::creatures::Entity* wolf = table.find(3);
    const swc::creatures::ScopeRecord* record = wolf != nullptr ? wolf->scope(0) : nullptr;
    check(record != nullptr, "the bite: a record of scope 0");
    if (record != nullptr) {
        check(record->startCount == 1 && record->fragment == 5 && record->action == 8, "the record: start count 1, fragment 5, action 8");
        check(near(record->speed, 1.0, 1e-6) && record->option == 0, "the playback scale 1.0 and the option 0");
        check(near(record->time, 0.5, 1.0 / 30.0), "the time of scope 0: 0.5 s in ticks of 1/30 s");
    }
    const auto changes = table.take_changes();
    check(changes.size() == 1 && changes.front().kind == swc::creatures::Change::Kind::action, "a change: a new action");
    // A later state with no scope fields (a step with keepScope): the table keeps the record.
    const std::uint32_t revision = wolf->scopeRevision;
    scene.clock.advance(std::chrono::milliseconds(200));
    scene.entity.position.x += 0.35F;
    table.apply(movement_record(scene, 3, 0x10001, true));
    check(wolf->scopeRevision == revision && wolf->scope(0) != nullptr && wolf->scope(0)->startCount == 1,
          "a state with no scope fields keeps the record");
    check(wolf->samples.size() == 2 && near(wolf->samples.back().position.x, 8815.60, 1e-3), "the step: a second movement sample");
    check(wolf->samples.back().timeUs - wolf->samples.front().timeUs == 200000, "the time anchors are 200 ms apart");
    // The same timed action in the next state: the same start count and a later time (user test 2, UT2-3).
    table.apply(movement_record(scene, 3, 0x10001, false));
    check(wolf->scope(0) != nullptr && wolf->scope(0)->startCount == 1 && near(wolf->scope(0)->time, 0.7, 1.0 / 30.0),
          "the timed action again: the same start count, the time 0.7 s");
    check(table.take_changes().empty(), "the same start count is no new action");
    // The end of the action: the fields with no record (the client plays its idle).
    scene.entity.creature.action.reset();
    table.apply(movement_record(scene, 3, 0x10001, false));
    check(wolf->hasScopes && wolf->scopeMask == 0 && wolf->scope(0) == nullptr, "the fields with no record end the action");
    // The additive reaction over the run (stage U3): the mask has bit 0 and bit 4, the time of scope 4 is a half float.
    backend::world::CreatureAction run;
    run.action = 42;
    run.fragment = 65;
    run.generation = 2;
    run.tags = scene.slice.tags;
    run.started = scene.clock.now() - std::chrono::milliseconds(1000);
    scene.entity.creature.moving = run;
    backend::world::CreatureAdditive additive;
    additive.scope = 4;
    additive.record.action = 26;
    additive.record.fragment = 35;
    additive.record.generation = 3;
    additive.record.tags = scene.slice.tags;
    additive.record.started = scene.clock.now() - std::chrono::milliseconds(300);
    scene.entity.creature.additive = additive;
    table.apply(movement_record(scene, 3, 0x10001, false));
    check(wolf->scopeMask == 0x11, "the additive reaction: the scopes 0 and 4");
    check(wolf->scope(0) != nullptr && wolf->scope(0)->fragment == 65 && wolf->scope(0)->action == 42 && near(wolf->scope(0)->time, 1.0, 1.0 / 30.0),
          "scope 0: the run (Move_Combat) after 1 s");
    check(wolf->scope(4) != nullptr && wolf->scope(4)->fragment == 35 && wolf->scope(4)->action == 26 && near(wolf->scope(4)->time, 0.3, 0.002),
          "scope 4: the additive reaction (r_R0_F) after 0.3 s");
}

void test_death_and_stop() {
    Scene scene;
    make_wolf(scene);
    EntityTable table;
    table.apply(first_record(scene, 4));
    table.take_changes();
    scene.entity.vitals.health = 500.0F;
    table.apply(vitals_record(scene, 4));
    const swc::creatures::Entity* wolf = table.find(4);
    check(wolf != nullptr && near(wolf->health, 500.0, 1e-3) && wolf->healthRevision == 2, "a health change with bit 0 of healthChangeFlags");
    auto changes = table.take_changes();
    check(changes.size() == 1 && changes.front().kind == swc::creatures::Change::Kind::health, "a change: the health");
    scene.entity.vitals.health = 0.0F;
    scene.entity.creature.dead = true;
    backend::world::CreatureAction death;
    death.action = 27;
    death.fragment = 4;
    death.generation = 4;
    death.tags = scene.slice.tags;
    death.started = scene.clock.now();
    scene.entity.creature.action = death;
    table.apply(vitals_record(scene, 4));
    table.apply(movement_record(scene, 4, backend::server::rep::kScopeNone, false));
    check(wolf->dead && near(wolf->health, 0.0, 1e-6), "the dead mark (bit 0 of vitalsData)");
    check(wolf->scope(0) != nullptr && wolf->scope(0)->fragment == 4 && wolf->scope(0)->action == 27, "the death action (r_Death, Death)");
    changes = table.take_changes();
    bool death_change = false;
    for (const auto& change : changes) {
        death_change = death_change || change.kind == swc::creatures::Change::Kind::death;
    }
    check(death_change, "a change: the death");
    const std::uint64_t serial = wolf->serial;
    table.stop(4);
    check(table.find(4) == nullptr, "a stop removes the entity");
    table.apply(first_record(scene, 4));
    check(table.find(4) != nullptr && table.find(4)->serial != serial, "a new entity of the same interest number has a new serial");
}

/** A forced motion (stage P3.5; backend forced_motion.cpp, protocol notes 4.26): the server moves the creature with a state each
 *  50 ms while the record of its reaction (the stagger r_R2_F) goes out again in each state with the same start count. */
void test_forced_motion() {
    Scene scene;
    make_wolf(scene);
    EntityTable table;
    table.apply(first_record(scene, 3));
    table.take_changes();
    backend::world::CreatureAction stagger;
    stagger.action = 25;
    stagger.fragment = 15;
    stagger.generation = 1;
    stagger.tags = scene.slice.tags;
    stagger.started = scene.clock.now();
    stagger.ends = stagger.started + std::chrono::milliseconds(1000);
    scene.entity.creature.action = stagger;
    table.apply(movement_record(scene, 3, backend::server::rep::kScopeNone, false));
    const swc::creatures::Entity* wolf = table.find(3);
    const std::size_t before = wolf->samples.size();
    int actionChanges = 0;
    for (const auto& change : table.take_changes()) {
        actionChanges += change.kind == swc::creatures::Change::Kind::action ? 1 : 0;
    }
    // Six steps of 50 ms of a push to the east: 0.12 m each.
    for (int step = 1; step <= 6; ++step) {
        scene.clock.advance(std::chrono::milliseconds(50));
        scene.entity.position.x += 0.12F;
        table.apply(movement_record(scene, 3, 0x10001, false));
        for (const auto& change : table.take_changes()) {
            actionChanges += change.kind == swc::creatures::Change::Kind::action ? 1 : 0;
        }
    }
    check(wolf->samples.size() == before + 6, "a sample for each state of the forced motion");
    check(wolf->samples.back().timeUs - wolf->samples[wolf->samples.size() - 2].timeUs == 50000, "the states of a forced motion are 50 ms apart");
    check(near(wolf->samples.back().position.x, 8815.25 + 0.72, 1e-3), "the place after six steps of 0.12 m");
    check(wolf->scope(0) != nullptr && wolf->scope(0)->startCount == 1 && wolf->scope(0)->fragment == 15 && near(wolf->scope(0)->time, 0.3, 1.0 / 30.0),
          "the reaction record in each state: the same start count, the time of the state");
    check(actionChanges == 1, "one action change: the same start count does not start the reaction again");
}

/** The status effects of a creature (stage P3.5; StatusEffects, type 4236, the compact map of group 3 field 1). */
void test_statuses() {
    Scene scene;
    make_wolf(scene);
    EntityTable table;
    table.apply(first_record(scene, 3));
    const auto record = [&](std::uint64_t stamp) {
        backend::server::rep::PartsRequest request;
        request.since = 7;
        request.clock = &scene.clock;
        scene.entity.changed[backend::world::ComponentKey::status] = stamp;
        decoder::Record out;
        out.interest = 3;
        for (const auto& part : backend::server::rep::encode_creature_status_state(scene.entity, backend::server::rep::View::observer, request)) {
            out.fragments.push_back(fragment_of(12, 4236, part, "status effects"));
        }
        return out;
    };
    backend::world::CreatureEffect stun;
    stun.status = "WarHammer_Shockwave_Stun";
    stun.index = 1710;
    stun.stacks = 1;
    stun.shown = true;
    scene.entity.creature.effects.push_back(stun);
    table.apply(record(5));
    const swc::creatures::Entity* wolf = table.find(3);
    check(wolf->hasStatuses && wolf->statusStamp == 5 && wolf->statuses.size() == 1, "the compact map: one status");
    check(!wolf->statuses.empty() && wolf->statuses[0].index == 1710 && wolf->statuses[0].stacks == 1 && wolf->statuses[0].potencyOne,
          "the stun of Shockwave (1710): one stack, the potency 1");
    bool changed = false;
    for (const auto& change : table.take_changes()) {
        changed = changed || change.kind == swc::creatures::Change::Kind::status;
    }
    check(changed, "a change of the kind status");
    // Three stacks of a slow (packed 0x83), with a hidden effect that is not in the map.
    backend::world::CreatureEffect slow;
    slow.status = "WarHammer_Ult_SlowOnCC";
    slow.index = 1728;
    slow.stacks = 3;
    slow.shown = true;
    backend::world::CreatureEffect hidden;
    hidden.status = "GreatAxe_Vortex";
    hidden.index = 3399;
    hidden.shown = false;
    scene.entity.creature.effects.push_back(slow);
    scene.entity.creature.effects.push_back(hidden);
    table.apply(record(8));
    check(wolf->statuses.size() == 2 && wolf->statuses[1].index == 1728 && wolf->statuses[1].stacks == 3, "the slow with three stacks; no hidden effect");
    // A list with a smaller stamp changes nothing; an empty list with a larger stamp removes each entry.
    scene.entity.creature.effects.clear();
    table.apply(record(6));
    check(wolf->statuses.size() == 2 && wolf->statusStamp == 8, "a smaller stamp changes nothing");
    table.apply(record(9));
    check(wolf->hasStatuses && wolf->statuses.empty() && wolf->statusStamp == 9, "an empty list removes each status");
}

void test_half_float() {
    check(swc::creatures::half_to_float(0x3C00) == 1.0F, "half float 0x3C00 is 1.0");
    check(near(swc::creatures::half_to_float(0x34CD), 0.3, 0.001), "half float 0x34CD is 0.3");
    check(swc::creatures::half_to_float(0) == 0.0F, "half float 0 is 0");
}

} // namespace

int main() {
    test_half_float();
    test_forced_motion();
    test_statuses();
    test_first_record();
    test_actions();
    test_death_and_stop();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
