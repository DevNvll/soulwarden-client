// Tests of the mana and the heal numbers of the owner (stage P3.6) with bytes that the backend makes: the mana state (slot 47, type
// 1652) and the prediction sync 4289 (backend states/mana_state.cpp, mana_messages.cpp), the owner vitals state with the heal number
// (slot 41, type 15: bit 1 of healthChangeFlags; states/vitals_state.cpp), and the heal of a friend 4298 (health_messages.cpp), into
// the client after the golden session of case T2. No capture of the user. Build and run: tools/test-ranged.sh.

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
#include "server/rep/health_messages.h"
#include "server/rep/mana_messages.h"
#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "support/client_frames.h"
#include "support/manual_clock.h"
#include "world/entity.h"

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

void test_mana(Session& session) {
    std::printf("the mana state (slot 47) and the prediction sync (4289): the encoders of the backend into the client\n");
    backend::tests::ManualClock clock;
    backend::world::Entity entity;
    entity.id = session.client->own().group;
    entity.vitals.mana = 84.0F;
    entity.vitals.manaMax = 110.0F;
    entity.vitals.manaBase = 100.0F;
    entity.vitals.manaRateFactor = 1.2F;
    entity.vitals.manaRegenRate = 2.4F;
    entity.vitals.manaRegenFrom = clock.now() + std::chrono::milliseconds(1500);
    const std::uint32_t revision = session.client->own().manaRevision;
    session.apply({{swc::net::kSlotMana, swc::net::kStateMana.index, rep::encode_mana_state(entity, rep::View::owner, clock)}});
    const auto& own = session.client->own();
    expect(own.manaRevision == revision + 1 && own.hasMana && near(own.mana, 84.0F, 1e-4F) && near(own.manaMax, 100.0F, 1e-4F),
           "the mana state: the value 84 and the base maximum 100");
    expect(near(own.manaDelay, 1.5F, 0.01F) && near(own.manaRate, 2.4F, 1e-4F), "the mana state: the delay 1.5 s and the rate 2.4 a second");
    const Bytes sync = rep::encode_mana_sync(0x1234, entity, clock);
    const std::uint32_t syncs = own.manaSyncRevision;
    session.client->apply_message_for_tests(swc::net::kManaForceSync.index, sync, session.now);
    expect(own.manaSyncRevision == syncs + 1 && near(own.manaSync.value, 84.0F, 1e-4F) && near(own.manaSync.maximum, 100.0F, 1e-4F)
               && near(own.manaSync.delay, 1.5F, 0.01F) && near(own.manaSync.factor, 1.2F, 1e-4F),
           "4289: the value, the base maximum, the delay, and the rate factor 1.2");
    // A sync of a different entity changes nothing.
    backend::world::Entity other = entity;
    other.id = entity.id + 1;
    session.client->apply_message_for_tests(swc::net::kManaForceSync.index, rep::encode_mana_sync(0x1234, other, clock), session.now);
    expect(own.manaSyncRevision == syncs + 1, "4289 of a different entity: no change");
}

void test_heals(Session& session) {
    std::printf("the heal numbers: the owner vitals with bit 1 of healthChangeFlags, and message 4298\n");
    backend::tests::ManualClock clock;
    backend::world::Entity entity;
    entity.id = session.client->own().group;
    entity.kind = backend::world::EntityKind::player;
    entity.vitals.healthMax = 1000.0F;
    entity.vitals.health = 400.0F;
    // The first state of the session (no life data).
    rep::PartsRequest first{0, 900};
    first.clock = &clock;
    Fragments fragments;
    for (Bytes& part : rep::encode_vitals_parts(entity, rep::View::owner, first)) {
        fragments.emplace_back(swc::net::kSlotVitals, swc::net::kStateVitals.index, std::move(part));
    }
    session.apply(fragments);
    const auto& own = session.client->own();
    expect(near(own.health, 400.0F, 1e-3F), "the first vitals state: the health 400");
    const std::uint32_t heals = own.healRevision;
    // Two Beacon pulses of 55.1 (the live value of protocol notes 4.28) after a silent gain of 10 (a food): three parts in one record.
    entity.vitals.healSteps = {{11, 410.0F, 465.1F, {}}, {12, 465.1F, 520.2F, {}}};
    entity.vitals.health = 520.2F;
    rep::PartsRequest later{10, 900};
    later.clock = &clock;
    later.ownerHealth = 400.0F;
    fragments.clear();
    for (Bytes& part : rep::encode_vitals_parts(entity, rep::View::owner, later)) {
        fragments.emplace_back(swc::net::kSlotVitals, swc::net::kStateVitals.index, std::move(part));
    }
    expect(fragments.size() == 3, "the backend: the silent gain, then one part for each heal");
    session.apply(fragments);
    expect(own.healRevision == heals + 2 && own.heals.size() >= 2, "two heal numbers (not the silent gain)");
    if (own.heals.size() >= 2) {
        const auto& a = own.heals[own.heals.size() - 2];
        const auto& b = own.heals.back();
        expect(a.healed == 0 && b.healed == 0 && near(a.amount, 55.0F, 1e-3F) && near(b.amount, 55.0F, 1e-3F), "the numbers +55 and +55 (the whole part)");
    }
    expect(near(own.health, 520.2F, 1e-2F), "the health after the heals");
    // The last state (bit 1, the health 520.2) again: the same health gives no second number (live check L3 of stage 3.3b-S6).
    session.apply({fragments.back()});
    expect(own.healRevision == heals + 2, "the same heal state again: no second number");
    // 4298: the owner healed a friend (Divine Embrace: +412, protocol notes 4.31).
    const std::uint64_t friendId = entity.id + 7;
    const Bytes healed = rep::healed_player_body(entity.id, friendId, 412.4F);
    session.client->apply_message_for_tests(swc::net::kHealedPlayer.index, healed, session.now);
    expect(own.healRevision == heals + 3 && !own.heals.empty() && own.heals.back().healed == rep::player_stats_entity(friendId)
               && near(own.heals.back().amount, 412.4F, 1e-3F),
           "4298: the heal of a friend, its Stats id, the amount 412.4");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: swc-ranged-state-tests GOLDEN_HEX\n");
        return 2;
    }
    Session session(argv[1]);
    expect(session.client->phase() == swc::net::RepClient::Phase::inGame, "the client is in the world after the golden session");
    test_mana(session);
    test_heals(session);
    std::printf("Result: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
