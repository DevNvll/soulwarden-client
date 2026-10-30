// C-13: the level change in one connection (protocol notes 4.66) against the backend. A backend of the test fixtures (ServerHost, the
// fake client of case IN4 of the backend tests) moves a character into an instance and back; each message that the backend sends goes
// to the RepClient of this client, in order. Two maps: a story instance of the main world level (the client stays in the world:
// the sequence reset, the new context, a travel to the start), and a level that the client does not have (a clean end with a cause).
// Build and run: tools/test-net.sh (the core library of the backend). The Soulwarden sources are read only.

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "Net/Codec/SwcRepClient.h"
#include "Net/Codec/SwcRepMessages.h"
#include "core/logging/log.h"
#include "gamedata/game_modes.h"
#include "server/rep/rep_session.h"
#include "support/server_host.h"
#include "support/test_players.h"
#include "world/world.h"

namespace {

namespace backend = soulwarden::backend;
namespace bt = soulwarden::backend::tests;
using Clock = std::chrono::steady_clock;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-0000000000d1";
constexpr backend::state::WorldPosition kHome{100.0F, 200.0F, 50.0F};
/** The start of the made-up story instance of the main world level (as QuestMedea: its own space, the level of the main world). */
constexpr backend::state::WorldPosition kStoryStart{300.0F, 400.0F, 50.0F};

int g_checks = 0;
int g_failures = 0;

void expect(bool condition, const std::string& text) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL %s\n", text.c_str());
    } else {
        std::printf("  ok %s\n", text.c_str());
    }
}

/** A map row of the game modes (the form of map_of of the backend case IN4). */
backend::gamedata::GameMap map_of(const char* id, const char* level, backend::state::WorldPosition start) {
    backend::gamedata::GameMap map;
    map.id = id;
    map.mode = id;
    map.level = level;
    map.terrain = std::string("coatlicue/") + level;
    map.starts = {backend::gamedata::ModeStart{start, 0.0F}};
    map.nameKey = std::string("@") + id;
    map.name = id;
    map.recommendedLevel = 25;
    map.teamCapacity = 0;
    map.dungeon = true;
    map.soloTrial = true;
    map.timeoutSeconds = 600;
    return map;
}

bool near(const swc::net::WorldPosition& a, const backend::state::WorldPosition& b) {
    return std::abs(a.x - b.x) < 0.6F && std::abs(a.y - b.y) < 0.6F;
}

} // namespace

int main() {
    std::printf("the level change in one connection (protocol notes 4.66) with the messages of a backend of the test fixtures\n");
    std::vector<std::string> lines;
    backend::log::set_sink([&lines](const std::string& line) { lines.push_back(line); });
    const auto logged = [&lines](const std::string& text) {
        std::size_t count = 0;
        for (const std::string& line : lines) {
            count += line.find(text) != std::string::npos ? 1 : 0;
        }
        return count;
    };

    bt::ServerHost host;
    expect(host.gameModes.set({map_of("Test_Story", "newworld_vitaeeterna", kStoryStart), map_of("Test_Dungeon", "nw_test_dungeon_00", {645.0F, 663.0F, 153.0F})}),
           "setup: the game modes (a story instance of the main world level and a dungeon level)");
    const bt::Player alpha = bt::add_player(host.store, bt::kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    bt::FakeClient a(27000);
    host.enter(a, alpha.registration());
    host.datagram(a.peer, a.report_at(1, kHome.x, kHome.y, kHome.z));
    host.steps(3, {&a});

    swc::net::RepClient::Options options;
    options.keepOtherRecords = true;
    options.token.characterId = kCharacterA;   // only the character of the login makes the record of interest 1 the local player
    swc::net::RepClient client(options);
    Clock::time_point now{};
    std::size_t fed = 0;
    const auto feed = [&]() {
        for (; fed < a.got.size(); ++fed) {
            const bt::Sent& sent = a.got[fed];
            now += std::chrono::milliseconds(10);
            if (sent.type == swc::net::kReplicatedStateBundle.index) {
                client.apply_bundle_for_tests(sent.body, now);
            } else {
                client.apply_message_for_tests(sent.type, sent.body, now);
            }
        }
    };
    const auto events = [&]() {
        std::string all;
        for (const auto& event : client.take_events()) {
            all += event.text + "\n";
        }
        return all;
    };
    feed();
    expect(client.phase() == swc::net::RepClient::Phase::inGame && client.level_info() && client.level_info()->instance == 1,
           "the world entry: InGame, the main world level, context instance 1");
    expect(near(client.own().position, kHome) && client.level_changes() == 0, "the world entry: the place of the character, no level change");
    const std::uint32_t entryTravels = client.own().travelRevision;
    (void)events();
    (void)client.take_other_updates();

    // A story instance of the main world level: the same level text, a new space.
    const backend::world::Entity& entity = *host.world.find_character(kCharacterA);
    expect(host.world.instances().enter(kCharacterA, "Test_Story").error == backend::world::InstanceError::none, "setup: the entry of the story instance");
    host.steps(3, {&a});
    feed();
    const std::string story = events();
    expect(client.phase() == swc::net::RepClient::Phase::inGame, "the story instance: the client is in the world again (InGame)");
    expect(client.level_changes() == 1 && client.level_info()->level == "newworld_vitaeeterna" && client.level_info()->instance == 2,
           "the story instance: one level change, the same level, the new context instance 2");
    expect(client.counts().droppedBundles == 0, "the story instance: no bundle dropped (the expected sequence is 1 again; before C-13 each later bundle was dropped)");
    expect(near(client.own().position, kStoryStart) && client.own().travelRevision == entryTravels + 1,
           "the story instance: the full owner record is a travel to the start of the instance");
    expect(story.find("the acknowledgement 5181 is sent") != std::string::npos, "the story instance: the client sends 5181");
    bool stopped = false;
    for (const auto& update : client.take_other_updates()) {
        stopped = stopped || update.stop;
    }
    expect(stopped || client.others().empty(), "the story instance: the entities of the old context stop before the records of the new one");
    // The bytes of the 5181 of this client to the backend: the session writes its line (the backend waits for nothing).
    const swc::net::Bytes ack = swc::net::ack_level_info_body(client.own().group, 2);
    expect(ack.size() == 17, "5181: 17 bytes (target, entity, the context instance)");
    host.datagram(a.peer, a.component_message(backend::server::rep::kTypeAckLevelInfoChanged, entity.actor.bytes, ack));
    host.steps(1, {&a});
    expect(logged("rep: the client acknowledged the level information: context instance 2 (the session has 2)") == 1,
           "5181: the backend reads the body of this client");
    feed();
    (void)events();

    // The return to the main world: the same level text again.
    expect(host.world.instances().leave(kCharacterA).error == backend::world::InstanceError::none, "setup: the return");
    host.steps(3, {&a});
    feed();
    (void)events();
    expect(client.phase() == swc::net::RepClient::Phase::inGame && client.level_changes() == 2 && client.level_info()->instance == 3
               && near(client.own().position, kHome) && client.counts().droppedBundles == 0,
           "the return: InGame, the second level change, context instance 3, the return place, no bundle dropped");

    // A level that the client does not have: a clean end with a cause for the landing screen.
    expect(host.world.instances().enter(kCharacterA, "Test_Dungeon").error == backend::world::InstanceError::none, "setup: the entry of the dungeon");
    host.steps(3, {&a});
    feed();
    (void)events();
    expect(client.phase() == swc::net::RepClient::Phase::ended && client.end_cause().find("nw_test_dungeon_00") != std::string::npos,
           "the dungeon level: the session ends with the level in its cause (" + client.end_cause() + ")");
    expect(client.level_changes() == 2, "the dungeon level: no third level change");

    backend::log::set_sink({});
    std::printf("level change tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
