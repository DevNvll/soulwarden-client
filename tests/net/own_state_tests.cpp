// Tests of the movement states of the owner (stage P3.3) with bytes that the backend makes: the speed table (StatMultiplierTable,
// slot 43, type 1525) and the status map (StatusEffects, slot 44, type 4236) of the encoders of the backend
// (apps/backend/src/server/rep/states) go into the client in state bundles after the golden session of case T2. No capture of the
// user. Build and run: tools/test-own-states.sh. The Soulwarden sources are read only.

#include <algorithm>
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
    std::printf("Result: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
