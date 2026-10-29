// Tests of the owner states with bytes that the backend makes: the speed table (StatMultiplierTable, slot 43, type 1525) and the
// status map (StatusEffects, slot 44, type 4236) of stage P3.3, and the read path of stage MVP-A: the attributes (slot 27, type 129),
// the mastery and trade skill ranks (slot 65, type 911), the point pools (slot 67, type 3681), the hit counts of the ability state
// (slot 46, type 185), the heart rune charge (slot 48, type 5257), the craft state (slot 63, type 205), and the entitlements (slot 19,
// type 3133). The encoders of the backend (apps/backend/src/server/rep/states) make the states; they go into the client in state
// bundles after the golden session of case T2. No capture of the user. Build and run: tools/test-movement.sh. The Soulwarden sources
// are read only.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "Net/Codec/SwcRepClient.h"
#include "Net/Codec/SwcRepMessages.h"
#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "support/client_frames.h"
#include "support/manual_clock.h"
#include "world/entity.h"
#include "world/stamina.h"

// The one function of world/stamina.cpp that the speed table encoder calls (a copy of its body), so that the test does not link the
// systems of the world.
float soulwarden::backend::world::Stamina::regeneration_multiplier(const Entity& character) {
    return std::max(1.0F + character.gear.staminaRate, 0.0F);
}

namespace {

using swc::net::Bytes;
using Clock = std::chrono::steady_clock;
namespace backend = soulwarden::backend;
namespace rep = backend::server::rep;
using Fragments = std::vector<std::tuple<std::uint32_t, std::uint32_t, Bytes>>;

/** The values of the golden scenario (tests/support/golden_scenario.h of the backend). */
constexpr const char* kGoldenCharacterId = "0a0a0a0a-1111-4222-8333-444444444444";

int g_checks = 0;
int g_failures = 0;

void expect(bool condition, const std::string& text) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL %s\n", text.c_str());
    }
}

bool near(float value, float wanted, float margin) {
    return std::fabs(value - wanted) <= margin;
}

std::vector<Bytes> read_golden(const std::string& path) {
    std::vector<Bytes> datagrams;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        Bytes bytes;
        for (std::size_t index = 0; index + 1 < line.size(); index += 2) {
            bytes.push_back(static_cast<std::uint8_t>(std::stoul(line.substr(index, 2), nullptr, 16)));
        }
        datagrams.push_back(std::move(bytes));
    }
    return datagrams;
}

/** A client in the world after the datagrams of the golden session (the form of tests/net/item_tests.cpp). */
struct Session {
    std::unique_ptr<swc::net::RepClient> client;
    Clock::time_point now{};
    std::uint64_t bundleSequence{0};

    explicit Session(const std::string& goldenPath) {
        swc::net::RepClient::Options options;
        options.token.characterId = kGoldenCharacterId;
        options.token.personaId = "persona";
        options.token.steamUserId = backend::tests::kSteamIdA;
        options.steamId = backend::tests::kSteamIdA;
        options.ticketHex = swc::net::local_ticket_hex(backend::tests::kSteamIdA, "Tester");
        client = std::make_unique<swc::net::RepClient>(options);
        client->start(now);
        for (const Bytes& datagram : read_golden(goldenPath)) {
            now += std::chrono::milliseconds(10);
            client->receive(datagram, now);
            (void)client->tick(now);
        }
        // The bundles of a connection are 1, 2, 3, ... (the golden session dropped none).
        bundleSequence = client->counts().bundles;
        client->take_events();
    }

    /** One state bundle with one record of interest 1 (the own entity), in the form of RepSession::send_state_bundle. */
    void apply(const Fragments& fragments) {
        rep::RecordWriter record(1);
        for (const auto& [slot, type, state] : fragments) {
            record.fragment(slot, type, state);
        }
        const Bytes payload = record.finish();
        backend::server::hub::Writer body;
        body.u8(1);
        body.size64(++bundleSequence);
        body.u8(1);   // the context instance of the level information of the golden session
        body.u8(0);
        body.boolean(false);
        body.boolean(false);
        body.size(static_cast<std::uint32_t>(payload.size()));
        body.raw(payload);
        now += std::chrono::milliseconds(10);
        client->apply_bundle_for_tests(body.data(), now);
    }
};

Fragments speed_fragments(const backend::world::Entity& entity, rep::View view, backend::world::Revision since) {
    Fragments fragments;
    for (Bytes& part : rep::encode_stat_multiplier_state(entity, view, rep::PartsRequest{since, 900})) {
        fragments.emplace_back(swc::net::kSlotSpeedTable, swc::net::kStateSpeedTable.index, std::move(part));
    }
    return fragments;
}

Fragments status_fragments(const backend::world::Entity& entity, backend::world::Revision since, const backend::world::WorldClock& clock) {
    rep::PartsRequest request{since, 900};
    request.clock = &clock;
    Fragments fragments;
    for (Bytes& part : rep::encode_player_status_state(entity, rep::View::owner, request)) {
        fragments.emplace_back(swc::net::kSlotStatuses, swc::net::kStateStatuses.index, std::move(part));
    }
    return fragments;
}

void test_speed_table(Session& session) {
    std::printf("the speed table (slot 43): the encoder of the backend into the client\n");
    backend::world::Entity entity;
    expect(speed_fragments(entity, rep::View::owner, 0).empty(), "the first record with the multipliers 1 has no fragment");
    // A slow of a creature (AI_Snare_6sec: SprintSpeedMod -0.5): the backend sends the key 0x35 at 0.5.
    entity.sprintMultiplier = 0.5F;
    entity.changed[backend::world::ComponentKey::statMultipliers] = 10;
    expect(speed_fragments(entity, rep::View::observer, 10).empty(), "the observer view has no fragment");
    Fragments fragments = speed_fragments(entity, rep::View::owner, 0);
    expect(fragments.size() == 1, "the owner view: one part");
    std::uint64_t revision = session.client->own().speedRevision;
    session.apply(fragments);
    {
        const auto& table = session.client->own().speedTable;
        const auto sprint = table.find(0x35);
        const auto move = table.find(0x34);
        expect(session.client->own().speedRevision == revision + 1, "the slow: one change of the table");
        expect(sprint != table.end() && near(sprint->second, 0.5F, 1e-4F), "the slow: the key 0x35 is 0.5");
        expect(move != table.end() && near(move->second, 1.0F, 1e-4F), "the slow: the key 0x34 is 1.0");
    }
    // The road haste (Traversal_RoadSprintBoost_Haste: SprintSpeedMod +0.15): 1.15, the sprint 5.5 x 1.15 = 6.325 m/s.
    entity.sprintMultiplier = 1.15F;
    entity.changed[backend::world::ComponentKey::statMultipliers] = 11;
    revision = session.client->own().speedRevision;
    session.apply(speed_fragments(entity, rep::View::owner, 10));
    {
        const auto& table = session.client->own().speedTable;
        const auto sprint = table.find(0x35);
        expect(session.client->own().speedRevision == revision + 1, "the haste: one change of the table");
        expect(sprint != table.end() && near(sprint->second, 1.15F, 1e-4F), "the haste: the key 0x35 is 1.15");
        expect(sprint != table.end() && near(5.5F * sprint->second, 6.325F, 1e-3F), "the haste: the sprint is 6.325 m/s");
    }
    // A stamina rate key next to the speed keys (key 0x0B): the client keeps it and reads the speed keys.
    entity.gear.staminaRate = 0.25F;
    entity.changed[backend::world::ComponentKey::statMultipliers] = 12;
    session.apply(speed_fragments(entity, rep::View::owner, 11));
    {
        const auto& table = session.client->own().speedTable;
        const auto stamina = table.find(0x0B);
        const auto sprint = table.find(0x35);
        expect(stamina != table.end() && near(stamina->second, 1.25F, 1e-4F), "the key 0x0B is 1.25");
        expect(sprint != table.end() && near(sprint->second, 1.15F, 1e-4F), "the key 0x35 stays 1.15");
    }
    // The end of the statuses: the empty table (an absent key is 1.0).
    entity.sprintMultiplier = 1.0F;
    entity.gear.staminaRate = 0.0F;
    entity.changed[backend::world::ComponentKey::statMultipliers] = 13;
    fragments = speed_fragments(entity, rep::View::owner, 12);
    expect(fragments.size() == 1, "the neutral table after a change: one part");
    revision = session.client->own().speedRevision;
    session.apply(fragments);
    expect(session.client->own().speedRevision == revision + 1 && session.client->own().speedTable.empty(), "the neutral table is empty");
    expect(session.client->counts().incompleteBundles == 0, "the client reads each bundle to its end");
}

void test_statuses(Session& session) {
    std::printf("the status map (slot 44): the encoder of the backend into the client\n");
    backend::tests::ManualClock clock;
    backend::world::Entity entity;
    expect(status_fragments(entity, 0, clock).empty(), "the first record with no status has no fragment");
    // A root (AI_Root_4sec) and a stun (AI_Stun_4sec) of a creature. The indexes are made up: the client finds the row from the
    // index in movement.json.
    backend::world::ActiveStatus root;
    root.status = "AI_Root_4sec";
    root.index = 2702;
    root.started = clock.now();
    root.ends = clock.now() + std::chrono::seconds(4);
    backend::world::ActiveStatus stun = root;
    stun.status = "AI_Stun_4sec";
    stun.index = 2703;
    stun.potency = 0.5F;
    stun.stacks = 2;
    // A status of the gear is not on the wire.
    backend::world::ActiveStatus gear = root;
    gear.status = "Perk";
    gear.index = 77;
    gear.permanent = true;
    gear.replicated = false;
    entity.statuses = {root, stun, gear};
    entity.changed[backend::world::ComponentKey::status] = 20;
    Fragments fragments = status_fragments(entity, 0, clock);
    expect(fragments.size() == 1, "the owner view: one part");
    std::uint64_t revision = session.client->own().statusRevision;
    session.apply(fragments);
    {
        const auto& statuses = session.client->own().statuses;
        expect(session.client->own().statusRevision == revision + 1, "one change of the map");
        expect(statuses.size() == 2, "two statuses (not the status of the gear)");
        if (statuses.size() == 2) {
            expect(statuses[0].index == 2702 && statuses[0].stacks == 1 && near(statuses[0].potency, 1.0F, 1e-3F),
                   "the root: its index, its stacks, its potency");
            expect(statuses[0].wallEndNs == clock.wall_time_ns(root.ends), "the root: its end on the wall clock");
            expect(statuses[1].index == 2703 && statuses[1].stacks == 2 && near(statuses[1].potency, 0.5F, 1e-3F),
                   "the stun: its index, its stacks, its potency (a half float)");
        }
    }
    // A map with an older stamp changes nothing (the rule of the client, live check L1 of stage 3.3b-S1 of the backend).
    backend::world::Entity older = entity;
    older.statuses = {stun};
    older.changed[backend::world::ComponentKey::status] = 19;
    revision = session.client->own().statusRevision;
    session.apply(status_fragments(older, 20, clock));
    expect(session.client->own().statusRevision == revision && session.client->own().statuses.size() == 2, "an older stamp changes nothing");
    // The root ends at its time: the backend sends the map with the stun alone.
    clock.advance(std::chrono::seconds(5));
    entity.statuses[1].ends = clock.now() + std::chrono::seconds(2);
    entity.changed[backend::world::ComponentKey::status] = 21;
    session.apply(status_fragments(entity, 20, clock));
    expect(session.client->own().statuses.size() == 1 && session.client->own().statuses[0].index == 2703, "the end of the root: the stun stays");
    // The end of all statuses: an empty map.
    entity.statuses.clear();
    entity.changed[backend::world::ComponentKey::status] = 22;
    fragments = status_fragments(entity, 21, clock);
    expect(fragments.size() == 1, "the empty map after a change: one part");
    session.apply(fragments);
    expect(session.client->own().statuses.empty(), "the empty map removes each status");
    expect(session.client->counts().incompleteBundles == 0, "the client reads each bundle to its end");
}

/** @return The progression id of a text (gamedata::progression_id: the CRC-32 of the lower-case text). */
std::uint32_t id_of(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return backend::server::hub::crc32(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

/** A level 70 character with the values of a full build (made up, the form of the user's character: five attributes with bonuses,
 *  two weapons at mastery 20, trade skills, a learned tree, a tracked hit count, a rune charge, a craft cooldown, the expansion). */
backend::world::Entity level70_character(const backend::world::WorldClock& clock) {
    backend::world::Entity entity;
    namespace attribute = backend::world::attribute;
    entity.attributes.spent[attribute::kStrength] = 300;
    entity.attributes.spent[attribute::kConstitution] = 150;
    entity.attributes.spent[attribute::kDexterity] = 40;
    entity.attributes.bonus[attribute::kStrength] = 35;
    entity.attributes.bonus[attribute::kConstitution] = 60;
    entity.attributes.bonus[attribute::kFocus] = 10;
    entity.attributes.unspent = 0;
    entity.skills.progressions[id_of("GreatAxe")] = {20, 1'512'000};
    entity.skills.progressions[id_of("WarHammer")] = {20, 1'512'000};
    entity.skills.progressions[id_of("Mining")] = {150, 2'000'000};
    entity.skills.progressions[id_of("Logging")] = {205, 9'000'000};
    // Two nodes in the great axe tree (levels 1 and 18: 1 point left), none in the war hammer tree (20 points).
    entity.skills.pools[id_of("GreatAxe")][id_of("greataxe_tree1_node1")] = {1, 5};
    entity.skills.pools[id_of("GreatAxe")][id_of("greataxe_tree1_node2")] = {18, 5};
    entity.skills.pools[id_of("WarHammer")] = {};
    entity.skills.bars[id_of("GreatAxeAbilityTable")] = {id_of("Ability_Greataxe_Whirlwind"), id_of("Ability_Greataxe_Charge"), 0};
    entity.combat.abilityHits = {{id_of("Ability_Greataxe_Whirlwind"), 4}};
    entity.runeCharge = 42.7F;
    backend::world::CraftCooldown window;
    window.recipe = "IngotT51";
    window.used = 3;
    window.end = clock.now() + std::chrono::hours(1);
    entity.craftCooldowns[id_of("IngotT51")] = window;
    entity.craftGearBonus[6] = 50;
    entity.entitlements = {47, 223, 1484};
    entity.achievements = {11979};
    for (const auto key : {backend::world::ComponentKey::categorical, backend::world::ComponentKey::points, backend::world::ComponentKey::abilities,
                           backend::world::ComponentKey::crafting, backend::world::ComponentKey::entitlements,
                           backend::world::ComponentKey::achievements}) {
        entity.changed[key] = 30;
    }
    return entity;
}

/** The fragments of the read path of stage MVP-A for the owner view, as the session of the backend sends them in a first record. */
Fragments progress_fragments(const backend::world::Entity& entity, backend::world::Revision since, const backend::world::WorldClock& clock) {
    rep::PartsRequest request{since, 900};
    request.clock = &clock;
    Fragments fragments;
    const auto add = [&fragments](std::uint32_t slot, std::uint32_t type, Bytes state) {
        if (!state.empty()) {
            fragments.emplace_back(slot, type, std::move(state));
        }
    };
    add(swc::net::kSlotAttribute, swc::net::kStateAttribute.index, rep::encode_attribute_state(entity, rep::View::owner, clock));
    add(swc::net::kSlotCategorical, swc::net::kStateCategorical.index, rep::encode_categorical_state(entity, rep::View::owner, clock));
    for (Bytes& part : rep::encode_points_state(entity, rep::View::owner, request)) {
        add(swc::net::kSlotPoints, swc::net::kStatePoints.index, std::move(part));
    }
    for (Bytes& part : rep::encode_ability_state(entity, rep::View::owner, request)) {
        add(swc::net::kSlotAbility, swc::net::kStateAbility.index, std::move(part));
    }
    for (Bytes& part : rep::encode_charge_state(entity, rep::View::owner, request)) {
        add(swc::net::kSlotCharge, swc::net::kStateCharge.index, std::move(part));
    }
    for (Bytes& part : rep::encode_crafting_state(entity, rep::View::owner, request)) {
        add(swc::net::kSlotCrafting, swc::net::kStateCrafting.index, std::move(part));
    }
    add(swc::net::kSlotEntitlement, swc::net::kStateEntitlement.index, rep::encode_entitlement_state(entity, rep::View::owner, clock));
    add(swc::net::kSlotAchievement, swc::net::kStateAchievement.index, rep::encode_achievement_state(entity, rep::View::owner, clock));
    return fragments;
}

void test_progress(Session& session) {
    std::printf("the read path of the owner states (slots 27, 65, 67, 46, 48, 63, 19): the encoders of the backend into the client\n");
    backend::tests::ManualClock clock;
    backend::world::Entity entity = level70_character(clock);
    expect(rep::encode_attribute_state(entity, rep::View::observer, clock).empty(), "the attributes: no fragment for an observer");
    Fragments fragments = progress_fragments(entity, 0, clock);
    const std::uint32_t attributeRevision = session.client->own().attributeRevision;
    expect(fragments.size() == 8, "the first record: eight fragments (" + std::to_string(fragments.size()) + ")");
    session.apply(fragments);
    const swc::net::RepClient::OwnEntity& own = session.client->own();
    namespace attribute = backend::world::attribute;
    // The attributes: the base value has the minimum 5 and the spent points; the bonus is apart (live check I4 of the backend).
    expect(own.hasAttributes && own.attributeRevision == attributeRevision + 1, "the attributes: one state");
    expect(own.attributes.base[attribute::kStrength] == 305 && own.attributes.bonus[attribute::kStrength] == 35, "STR 305 + 35");
    expect(own.attributes.base[attribute::kConstitution] == 155 && own.attributes.bonus[attribute::kConstitution] == 60, "CON 155 + 60");
    expect(own.attributes.base[attribute::kDexterity] == 45 && own.attributes.bonus[attribute::kDexterity] == 0, "DEX 45 + 0");
    expect(own.attributes.base[attribute::kFocus] == 5 && own.attributes.bonus[attribute::kFocus] == 10, "FOC 5 + 10");
    expect(own.attributes.base[attribute::kIntelligence] == 5, "INT 5");
    expect(own.attributes.spent[attribute::kStrength] == 300 && own.attributes.unspent == 0, "the spent points and the free points");
    // The ranks: each mastery and trade skill of the state; an id that is not in the state has none.
    expect(own.ranks.size() == 4, "four ranks");
    {
        const auto mining = own.ranks.find(id_of("Mining"));
        const auto logging = own.ranks.find(id_of("Logging"));
        const auto axe = own.ranks.find(id_of("GreatAxe"));
        expect(mining != own.ranks.end() && mining->second.rank == 150 && mining->second.points == 2'000'000, "Mining 150");
        expect(logging != own.ranks.end() && logging->second.rank == 205, "Logging 205");
        expect(axe != own.ranks.end() && axe->second.rank == 20 && axe->second.points == 1'512'000, "the great axe mastery 20");
        expect(own.ranks.count(id_of("Harvesting")) == 0, "no Harvesting entry (rank 0)");
    }
    // The point pools: the free points of each tree (rank minus the spent levels).
    {
        const auto axe = own.pointPools.find(id_of("GreatAxe"));
        const auto hammer = own.pointPools.find(id_of("WarHammer"));
        expect(own.pointPools.size() == 2, "two point pools");
        expect(axe != own.pointPools.end() && axe->second == 1, "the great axe pool: 1 free point");
        expect(hammer != own.pointPools.end() && hammer->second == 20, "the war hammer pool: 20 free points");
        expect(own.learnedNodes.size() == 2, "the two learned nodes stay");
    }
    // The hit counts of the ability state (fields 1 and 2).
    {
        const auto whirlwind = own.abilityHits.find(id_of("Ability_Greataxe_Whirlwind"));
        expect(own.hitRevision == 1 && whirlwind != own.abilityHits.end() && whirlwind->second == 4, "Whirlwind: 4 hits");
        expect(!own.abilityBars.empty(), "the bars of the same fragment");
    }
    // The heart rune charge: the whole percent (42.7 -> 42).
    expect(own.hasCharge && own.chargePercent == 42, "the charge: 42 percent");
    // The craft state: the window of the recipe with its end on the wall clock, and the bonus of Engineering (code 6).
    expect(own.craftRevision == 1 && own.craftCooldowns.size() == 1, "the craft state: one cooldown window");
    if (own.craftCooldowns.size() == 1) {
        expect(own.craftCooldowns[0].recipe == id_of("IngotT51") && own.craftCooldowns[0].used == 3, "the window of IngotT51: 3 crafts");
        expect(own.craftCooldowns[0].wallEndNs == clock.wall_time_ns(entity.craftCooldowns.begin()->second.end), "the end on the wall clock");
    }
    expect(own.craftGearBonus.size() == 1 && own.craftGearBonus.count(6) == 1 && own.craftGearBonus.at(6) == 50, "Engineering +50 gear score");
    // The entitlements and the expansion achievement.
    expect(own.entitlementsReceived && swc::net::has_bit(own.entitlements, 1484) && swc::net::has_bit(own.entitlements, 47)
               && !swc::net::has_bit(own.entitlements, 48),
           "the entitlements: 47, 223, 1484 (the expansion), the mark received");
    expect(swc::net::has_bit(own.achievements, 11979), "the achievement Expansion_2023_Unlock");

    // Later states: a food gives STR +20 (the bonus map changes alone), the mastery of the war hammer goes up, a node goes away, the
    // charge empties (a state with 0 after a record), the craft window ends (empty maps after a change), the hit count is reset.
    entity.attributes.bonus[attribute::kStrength] = 55;
    entity.skills.progressions[id_of("WarHammer")] = {21, 1'600'000};
    entity.skills.progressions.erase(id_of("Mining"));
    entity.skills.pools[id_of("GreatAxe")][id_of("greataxe_tree1_node2")] = {0, 31};
    entity.combat.abilityHits = {{id_of("Ability_Greataxe_Whirlwind"), 0}};
    entity.runeCharge = 0.0F;
    clock.advance(std::chrono::hours(2));
    entity.craftCooldowns.clear();
    entity.craftGearBonus.clear();
    for (const auto key : {backend::world::ComponentKey::categorical, backend::world::ComponentKey::points, backend::world::ComponentKey::abilities,
                           backend::world::ComponentKey::crafting}) {
        entity.changed[key] = 31;
    }
    session.apply(progress_fragments(entity, 30, clock));
    expect(own.attributeRevision == attributeRevision + 2 && own.attributes.bonus[attribute::kStrength] == 55 && own.attributes.base[attribute::kStrength] == 305,
           "the food: STR 305 + 55");
    expect(own.ranks.count(id_of("Mining")) == 0 && own.ranks.size() == 3, "the full map replaces the old map: no Mining");
    expect(own.ranks.count(id_of("WarHammer")) == 1 && own.ranks.at(id_of("WarHammer")).rank == 21, "the war hammer mastery 21");
    expect(own.pointPools.at(id_of("GreatAxe")) == 19 && own.learnedNodes.size() == 1, "the lost node: 19 free points, one node");
    expect(own.pointPools.at(id_of("WarHammer")) == 21, "the war hammer pool: 21 free points");
    expect(own.abilityHits.at(id_of("Ability_Greataxe_Whirlwind")) == 0, "the reset hit count");
    expect(own.chargePercent == 0 && own.chargeRevision == 2, "the empty charge");
    expect(own.craftRevision == 2 && own.craftCooldowns.empty() && own.craftGearBonus.empty(), "the end of the craft window: empty maps");

    // A categorical state with an older stamp changes nothing (the merge of the client keeps its list).
    backend::world::Entity older = entity;
    older.skills.progressions[id_of("Mining")] = {150, 2'000'000};
    older.changed[backend::world::ComponentKey::categorical] = 29;
    const std::uint32_t rankRevision = own.rankRevision;
    session.apply({{swc::net::kSlotCategorical, swc::net::kStateCategorical.index, rep::encode_categorical_state(older, rep::View::owner, clock)}});
    expect(own.rankRevision == rankRevision && own.ranks.count(id_of("Mining")) == 0, "an older stamp changes nothing");
    // Stage Q7 (protocol notes 4.57; live I0 of the backend stage 1.8d): the territory standing is an entry of the same map, with the id
    // CRC-32 of the territory number (Windsward "6": 1DB87A14), the rank, and the points (rank 0 and 120 points after 06_raisealarm).
    entity.skills.progressions[0x1DB87A14U] = {0, 120};
    entity.changed[backend::world::ComponentKey::categorical] = 32;
    session.apply({{swc::net::kSlotCategorical, swc::net::kStateCategorical.index, rep::encode_categorical_state(entity, rep::View::owner, clock)}});
    {
        const auto standing = own.ranks.find(0x1DB87A14U);
        expect(standing != own.ranks.end() && standing->second.rank == 0 && standing->second.points == 120,
               "the standing of Windsward (1DB87A14): rank 0, 120 points");
        expect(own.ranks.count(id_of("WarHammer")) == 1, "the masteries stay with the standing");
    }
    // Step 3 of ut567 (QG1, protocol notes 4.59): the territory pool of Windsward (TerritoryRankBonus_6, CCDDF19B, category 2) after the
    // weapon pools in one fragment: its free tokens (the rank 2 of the standing minus the spent levels) and its cards with their levels.
    // TB_PurchaseHouse has DoNotSpendPoint: its level does not spend a token. The cards are not learned nodes of the weapon trees.
    const std::uint32_t windsward = 0xCCDDF19BU;
    const std::uint32_t gainStanding = 0x68C1F9AAU;
    const std::uint32_t purchaseHouse = id_of("TB_PurchaseHouse");
    entity.skills.progressions[0x1DB87A14U] = {2, 70};
    backend::world::TerritoryPool pool;
    pool.standing = 0x1DB87A14U;
    pool.cards = {{gainStanding, 1}, {purchaseHouse, 1}};
    pool.spent = 1;
    entity.skills.territoryPools[windsward] = pool;
    entity.changed[backend::world::ComponentKey::categorical] = 33;
    entity.changed[backend::world::ComponentKey::points] = 33;
    const std::vector<std::uint32_t> weaponNodes = own.learnedNodes;
    session.apply(progress_fragments(entity, 32, clock));
    expect(own.pointPools.count(windsward) == 1 && own.pointPools.at(windsward) == 1, "the territory pool of Windsward: 1 free token (rank 2 - 1)");
    expect(own.poolCategories.count(windsward) == 1 && own.poolCategories.at(windsward) == swc::net::kPoolCategoryTerritory,
           "the territory pool has the category 2");
    expect(own.poolCategories.at(id_of("GreatAxe")) == 1, "a weapon pool has the category 1");
    expect(own.learnedNodes == weaponNodes, "the cards are not learned nodes; the weapon nodes stay");
    {
        bool standingCard = false, houseCard = false;
        for (const auto& entry : own.territoryEntries) {
            standingCard = standingCard || (entry.pool == windsward && entry.point == gainStanding && entry.level == 1);
            houseCard = houseCard || (entry.pool == windsward && entry.point == purchaseHouse && entry.level == 1);
        }
        expect(own.territoryEntries.size() == 2 && standingCard && houseCard, "the two cards of Windsward with their levels");
    }
    // A card at level 2 (a repeated card): the level stays (a set of learned nodes would keep only 1).
    entity.skills.progressions[0x1DB87A14U] = {3, 10};
    entity.skills.territoryPools[windsward].cards[gainStanding] = 2;
    entity.skills.territoryPools[windsward].spent = 2;
    entity.changed[backend::world::ComponentKey::categorical] = 34;
    entity.changed[backend::world::ComponentKey::points] = 34;
    session.apply(progress_fragments(entity, 33, clock));
    {
        std::uint16_t level = 0;
        for (const auto& entry : own.territoryEntries) {
            level = entry.point == gainStanding ? entry.level : level;
        }
        expect(level == 2 && own.pointPools.at(windsward) == 1, "TB_GainStanding at level 2; 1 free token (rank 3 - 2)");
    }
    // A state with no territory pool removes it and its cards.
    entity.skills.territoryPools.clear();
    entity.changed[backend::world::ComponentKey::points] = 35;
    session.apply(progress_fragments(entity, 34, clock));
    expect(own.pointPools.count(windsward) == 0 && own.poolCategories.count(windsward) == 0 && own.territoryEntries.empty(),
           "a state with no territory pool: no pool, no card");
    expect(session.client->counts().incompleteBundles == 0, "the client reads each bundle to its end");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: swc-own-state-tests GOLDEN_HEX\n");
        return 2;
    }
    Session session(argv[1]);
    expect(session.client->phase() == swc::net::RepClient::Phase::inGame, "the client is in the world after the golden session");
    test_speed_table(session);
    test_statuses(session);
    test_progress(session);
    std::printf("Result: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
