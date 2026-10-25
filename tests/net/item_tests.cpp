// Tests of the bag and the equipment of the client (stage P2.4) with bytes that the backend makes: the states of the encoders of
// the backend (Container, Paperdoll, Currency: apps/backend/src/server/rep/states) go into the client in state bundles after the
// golden session of case T2, and the requests of the client go to the readers of the backend (server/rep/item_requests.cpp).
// No capture of the user. Build and run: tools/test-items.sh. The Soulwarden sources are read only.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "Net/Codec/SwcCarrier.h"
#include "Net/Codec/SwcRepClient.h"
#include "Net/Codec/SwcRepMessages.h"
#include "server/carrier/carrier.h"
#include "server/rep/item_requests.h"
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

/** A client in the world after the datagrams of the golden session, and a carrier of the backend that reads its datagrams. */
struct Session {
    std::unique_ptr<swc::net::RepClient> client;
    Clock::time_point now{};
    std::vector<std::pair<std::uint8_t, Bytes>> serverGot;
    std::unique_ptr<backend::server::carrier::Connection> serverView;
    std::uint64_t bundleSequence{0};

    explicit Session(const std::string& goldenPath) {
        swc::net::RepClient::Options options;
        options.token.characterId = kGoldenCharacterId;
        options.token.personaId = "persona";
        options.token.steamUserId = backend::tests::kSteamIdA;
        options.steamId = backend::tests::kSteamIdA;
        options.ticketHex = swc::net::local_ticket_hex(backend::tests::kSteamIdA, "Tester");
        client = std::make_unique<swc::net::RepClient>(options);
        serverView = std::make_unique<backend::server::carrier::Connection>(
            [](std::span<const std::uint8_t>) {},
            [this](std::uint8_t channel, std::span<const std::uint8_t> data) { serverGot.emplace_back(channel, Bytes(data.begin(), data.end())); });
        client->start(now);
        for (const Bytes& datagram : read_golden(goldenPath)) {
            now += std::chrono::milliseconds(10);
            client->receive(datagram, now);
            tick();
        }
        // The bundles of a connection are 1, 2, 3, ... (the golden session dropped none).
        bundleSequence = client->counts().bundles;
        client->take_events();
    }

    void tick() {
        for (const Bytes& out : client->tick(now)) {
            serverView->receive(out);
        }
    }

    /** One state bundle with one record of interest 1 (the own entity), in the form of RepSession::send_state_bundle. */
    void apply(const std::vector<std::tuple<std::uint32_t, std::uint32_t, Bytes>>& fragments) {
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

    /** @return The bodies of the component calls of one type that the backend carrier got since the last call. */
    std::vector<Bytes> calls(std::uint32_t type) {
        std::vector<Bytes> bodies;
        for (const auto& [channel, frame] : serverGot) {
            std::span<const std::uint8_t> payload;
            if (!backend::server::hub::decode_frame(frame, payload) || payload.size() < 19) {
                continue;
            }
            backend::server::hub::Reader reader(payload.subspan(16));
            reader.u8();
            reader.u8();
            if (reader.size() == type) {
                const auto body = payload.subspan(16 + reader.position());
                bodies.emplace_back(body.begin(), body.end());
            }
        }
        serverGot.clear();
        return bodies;
    }
};

backend::world::ItemInstance item(const char* id, std::uint32_t index, std::uint32_t quantity, std::uint32_t gearScore, std::uint8_t mark) {
    backend::world::ItemInstance value;
    value.itemId = id;
    value.index = index;
    value.quantity = quantity;
    value.gearScore = gearScore;
    value.durability = gearScore > 0 ? 900 : 0;
    value.maxDurability = gearScore > 0 ? 1000 : 0;
    value.chrono = mark;
    for (std::size_t place = 0; place < value.instance.size(); ++place) {
        value.instance[place] = static_cast<std::uint8_t>(mark + place);
    }
    return value;
}

bool same_instance(const std::array<std::uint8_t, 16>& one, const std::array<std::uint8_t, 16>& two) {
    return std::equal(one.begin(), one.end(), two.begin());
}

void test_states(const std::string& goldenPath) {
    std::printf("the bag, the paperdoll, and the coin: the states of the encoders of the backend into the client\n");
    Session session(goldenPath);
    expect(session.client->phase() == swc::net::RepClient::Phase::inGame, "the client is in the world after the golden session");
    backend::world::Entity entity;
    // Item indexes of the protocol notes 4.10: 1hSwordT1 5278, OreT1 2956; 1323 the chest armor of the equipment examples.
    const auto sword = item("1hSwordT1", 5278, 1, 100, 0x10);
    const auto ore = item("OreT1", 2956, 25, 0, 0x30);
    const auto chest = item("LightChestT1", 1323, 1, 60, 0x50);
    entity.bag.slots.resize(3);
    entity.bag.slots[0].item = sword;
    entity.bag.slots[0].changed = 1;
    entity.bag.slots[1].changed = 1;
    entity.bag.slots[2].item = ore;
    entity.bag.slots[2].changed = 1;
    entity.equipment.slots[1].item = chest;
    entity.equipment.slots[1].changed = 1;
    entity.wallet.coin = 12345;
    entity.progression.level = 5;
    entity.progression.experience = 1234;
    const rep::PartsRequest fresh{0, 900};
    backend::tests::ManualClock clock;

    std::vector<std::tuple<std::uint32_t, std::uint32_t, Bytes>> fragments;
    for (const Bytes& part : rep::encode_container_state(entity, rep::View::owner, fresh)) {
        fragments.emplace_back(swc::net::kSlotBag, swc::net::kStateContainer.index, part);
    }
    for (const Bytes& part : rep::encode_paperdoll_state(entity, rep::View::owner, fresh)) {
        fragments.emplace_back(swc::net::kSlotPaperdoll, swc::net::kStatePaperdoll.index, part);
    }
    fragments.emplace_back(swc::net::kSlotCurrency, swc::net::kStateCurrency.index, rep::encode_currency_state(entity, rep::View::owner, clock));
    fragments.emplace_back(swc::net::kSlotProgression, swc::net::kStateProgression.index,
                           rep::encode_progression_state(entity, rep::View::owner, clock));
    expect(fragments.size() == 4, "the encoders give one part for the bag, the paperdoll, the coin, and the progression");
    const auto bagBefore = session.client->own().bagRevision;
    session.apply(fragments);
    const auto& own = session.client->own();
    expect(session.client->counts().incompleteBundles == 0 && session.client->counts().droppedBundles == 0, "the client reads the bundle to its end");
    expect(own.bag.size() == 3 && own.bagRevision == bagBefore + 1, "the bag: 3 slots, one change");
    if (own.bag.size() == 3) {
        expect(own.bag[0].index == 5278 && own.bag[0].quantity == 1 && own.bag[0].gearScore == 100 && own.bag[0].durability == 900
                   && own.bag[0].maxDurability == 1000 && same_instance(own.bag[0].instance, sword.instance),
               "bag slot 0: the sword (index, quantity, gear score, durability, instance)");
        expect(own.bag[1].empty(), "bag slot 1: empty");
        expect(own.bag[2].index == 2956 && own.bag[2].quantity == 25 && same_instance(own.bag[2].instance, ore.instance), "bag slot 2: 25 ore");
    }
    const auto chestEntry = own.paperdollItems.find(1);
    expect(chestEntry != own.paperdollItems.end() && chestEntry->second.index == 1323 && chestEntry->second.gearScore == 60
               && same_instance(chestEntry->second.instance, chest.instance) && own.equipment.at(1) == 1323,
           "the paperdoll: the chest armor in slot 1 with its instance and gear score");
    expect(own.hasCoin && own.coin == 12345, "the coin: 12345 hundredths (123.45)");
    expect(own.hasProgression && own.level == 4 && own.experience == 1234, "the progression: the wire level 4 (level 5 of the screen), 1234 experience");

    // A journal: bag slot 1 gets 3 potions (revision 6 after the record of revision 5).
    entity.bag.slots[1].item = item("PotionHealthT1", 4100, 3, 0, 0x70);
    entity.bag.slots[1].changed = 6;
    fragments.clear();
    for (const Bytes& part : rep::encode_container_state(entity, rep::View::owner, rep::PartsRequest{5, 900})) {
        fragments.emplace_back(swc::net::kSlotBag, swc::net::kStateContainer.index, part);
    }
    session.apply(fragments);
    expect(own.bag.size() == 3 && own.bag[1].index == 4100 && own.bag[1].quantity == 3 && own.bag[0].index == 5278,
           "a journal of the backend sets one slot and keeps the others");

    // The unequip of the chest: the paperdoll entry of the emptied slot (cause 5); the chest goes to bag slot 3.
    entity.equipment.slots[1].item.reset();
    entity.equipment.slots[1].action = backend::world::sync::kItemUnequipped;
    entity.equipment.slots[1].changed = 7;
    entity.bag.slots.resize(4);
    entity.bag.slots[3].item = chest;
    entity.bag.slots[3].action = backend::world::sync::kItemUnequipped;
    entity.bag.slots[3].changed = 7;
    fragments.clear();
    for (const Bytes& part : rep::encode_paperdoll_state(entity, rep::View::owner, rep::PartsRequest{6, 900})) {
        fragments.emplace_back(swc::net::kSlotPaperdoll, swc::net::kStatePaperdoll.index, part);
    }
    for (const Bytes& part : rep::encode_container_state(entity, rep::View::owner, rep::PartsRequest{6, 900})) {
        fragments.emplace_back(swc::net::kSlotBag, swc::net::kStateContainer.index, part);
    }
    const auto paperdollBefore = own.paperdollRevision;
    session.apply(fragments);
    expect(own.paperdollItems.find(1) == own.paperdollItems.end() && own.equipment.at(1) == 0 && own.paperdollRevision > paperdollBefore,
           "the unequip: the paperdoll slot has no item");
    expect(own.bag.size() == 4 && own.bag[3].index == 1323 && same_instance(own.bag[3].instance, chest.instance), "the chest is in the new bag slot 3");
}

void test_requests(const std::string& goldenPath) {
    std::printf("the requests of the bag and of the equipment against the readers of the backend\n");
    // The targets of the rule of 4.2 against the values of the research (inventory.md 9, G = 5050505050505050) and the paperdoll
    // target of the captures (protocol notes 4.11: 0C47D9835971E8A3 for serial 1 with the tag "AZOT").
    const std::uint64_t research = 0x5050505050505050ULL;
    expect(swc::net::inventory_target(research, swc::net::kInventoryContainer) == 0xd9e6314081657a6eULL, "the target of the bag Container");
    expect(swc::net::inventory_target(research, swc::net::kPaperdollComponent) == 0x0828cf72a9c198f1ULL, "the target of the Paperdoll");
    expect(research + swc::net::kItemDropComponent == 0x33ea74d42ca2a965ULL, "the target of the ItemDrop component of the root");
    const swc::net::ActorId azot{0x02, 0x00, 0x00, 0x00, 0x41, 0x5A, 0x4F, 0x54, 0x10, 0x00, 0x00, 0x00, 0x41, 0x5A, 0x4F, 0x54};
    expect(swc::net::inventory_target(swc::net::group_of(azot), swc::net::kPaperdollComponent) == 0x0C47D9835971E8A3ULL,
           "the paperdoll target of the captures of the notes");

    Session session(goldenPath);
    auto& client = *session.client;
    const std::uint64_t group = client.own().group;
    swc::net::ItemInstanceId sword{};
    for (std::size_t place = 0; place < sword.size(); ++place) {
        sword[place] = static_cast<std::uint8_t>(0x10 + place);
    }
    const swc::net::ItemInstanceId none{};

    client.equip_item(0, 24, 1, sword);
    session.tick();
    auto bodies = session.calls(swc::net::kEquipItem.index);
    expect(bodies.size() == 1 && bodies[0].size() == 45, "3895: one call of 45 bytes on the reliable channel");
    if (bodies.size() == 1) {
        const auto read = rep::read_equip_request(bodies[0]);
        expect(read && read->call.entity == group && read->call.target == swc::net::inventory_target(group, swc::net::kPaperdollComponent),
               "3895: the backend reads the entity and the paperdoll target");
        expect(read && read->bagSlot == 0 && read->paperdollSlot == 24 && read->quantity == 1 && read->instance == sword && !read->flag,
               "3895: bag slot 0 to the weapon slot 24, quantity 1, the instance, flag 0");
    }
    client.unequip_item(1, -1, 1);
    session.tick();
    bodies = session.calls(swc::net::kUnequipItem.index);
    expect(bodies.size() == 1 && bodies[0].size() == 29, "3896: one call of 29 bytes");
    if (bodies.size() == 1) {
        const auto read = rep::read_unequip_request(bodies[0]);
        expect(read && read->paperdollSlot == 1 && read->bagSlot == -1 && read->quantity == 1 && read->flag,
               "3896: paperdoll slot 1 to the first free bag slot (-1), flag 1 (the capture)");
    }
    client.swap_items(25, 24, none, sword);
    session.tick();
    bodies = session.calls(swc::net::kSwapItems.index);
    expect(bodies.size() == 1 && bodies[0].size() == 57, "3900: one call of 57 bytes");
    if (bodies.size() == 1) {
        const auto read = rep::read_swap_request(bodies[0]);
        expect(read && read->first == 25 && read->second == 24 && read->firstInstance == none && read->secondInstance == sword && read->count == 0,
               "3900: the slot of the drop, the slot of the drag, their instances");
    }
    client.use_item(2);
    session.tick();
    bodies = session.calls(swc::net::kUseItem.index);
    expect(bodies.size() == 1 && bodies[0].size() == 20, "2917: one call of 20 bytes");
    if (bodies.size() == 1) {
        const auto call = rep::read_component_call(bodies[0]);
        backend::server::hub::Reader reader(std::span<const std::uint8_t>(bodies[0]).subspan(16));
        expect(call && call->entity == group && call->target == swc::net::inventory_target(group, swc::net::kInventoryContainer)
                   && reader.u32() == 2,
               "2917: the target of the bag Container and the bag slot 2");
    }
    swc::net::ItemInstanceId ore{};
    ore.fill(0x30);
    client.drop_items({swc::net::DropItemEntry{2, ore, 5}});
    session.tick();
    bodies = session.calls(swc::net::kDropInventoryItems.index);
    expect(bodies.size() == 1 && bodies[0].size() == 17 + 28, "1838: one call of 17 + 28 bytes");
    if (bodies.size() == 1) {
        const auto read = rep::read_drop_request(bodies[0]);
        expect(read && read->call.target == group + swc::net::kItemDropComponent && read->entries.size() == 1 && read->entries[0].slot == 2
                   && read->entries[0].quantity == 5 && read->entries[0].instance == ore,
               "1838: the ItemDrop target and the entry (bag slot 2, the instance, 5)");
    }
    client.drop_paperdoll_item(27, 1);
    session.tick();
    bodies = session.calls(swc::net::kDropPaperdollItem.index);
    expect(bodies.size() == 1 && bodies[0].size() == 24, "826: one call of 24 bytes");
    if (bodies.size() == 1) {
        const auto read = rep::read_paperdoll_drop_request(bodies[0]);
        expect(read && read->slot == 27 && read->quantity == 1, "826: paperdoll slot 27, quantity 1");
    }
    // Stage Q7: the open of a loot container (1733, protocol notes 4.57), read by the reader of the backend.
    swc::net::ItemInstanceId pouch{};
    pouch.fill(0x26);
    client.open_container_item(1, pouch, 1);
    session.tick();
    bodies = session.calls(swc::net::kInitiateSalvage.index);
    expect(bodies.size() == 1 && bodies[0].size() == 48, "1733: one call of 48 bytes");
    if (bodies.size() == 1) {
        const auto read = rep::read_salvage_request(bodies[0]);
        expect(read && read->call.entity == group && read->call.target == swc::net::crafting_target(group) && read->containerType == 1
                   && read->slot == 1 && read->instance == pouch && read->locationQuantity == 0 && read->quantity == 1,
               "1733: the Crafting facet, the bag (1), bag slot 1, the instance, location quantity 0, quantity 1");
    }
    expect(swc::net::kInitiateSalvage.index == rep::kTypeInitiateSalvage, "the type number of 1733 is the number of the backend");
    // The refusal: the body of the backend (the target and the entity of the request) reads as a call of the own entity.
    const Bytes fail = rep::equip_fail_body(rep::ComponentCall{swc::net::inventory_target(group, swc::net::kPaperdollComponent), group}, 0);
    const auto failCall = swc::net::read_component_call(fail);
    expect(fail.size() == 20 && failCall && failCall->entity == group, "3626: the client reads the call of the refusal");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: swc-item-tests GOLDEN_FILE\n");
        return 2;
    }
    test_states(argv[1]);
    test_requests(argv[1]);
    std::printf("items: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
