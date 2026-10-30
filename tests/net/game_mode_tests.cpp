// C-13 step 1b: the membership of a story instance (protocol notes 4.67) against the backend. A backend of the test fixtures (ServerHost, the
// fake client of case IN4 of the backend tests) moves a character into an instance; each message that the backend sends goes to the
// RepClient of this client, in order. The checks: the GameModeParticipant state (slot 24, 3312) in OwnEntity at the entry and after the
// leave, the result 3094 of an entrance, and the bodies of the leave 3309 and of the client event 1980 of this client, which the backend
// reads (it leaves the mode). Build and run: tools/test-net.sh (the core library of the backend). The Soulwarden sources are read only.

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "Net/Codec/SwcRepClient.h"
#include "Net/Codec/SwcRepMessages.h"
#include "core/logging/log.h"
#include "gamedata/game_modes.h"
#include "server/rep/instance_messages.h"
#include "server/rep/rep_session.h"
#include "support/server_host.h"
#include "support/test_players.h"
#include "world/world.h"

namespace {

namespace backend = soulwarden::backend;
namespace bt = soulwarden::backend::tests;
using Clock = std::chrono::steady_clock;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-0000000000d2";
constexpr backend::state::WorldPosition kHome{100.0F, 200.0F, 50.0F};
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

/** A map row of the game modes (the form of map_of of the backend case IN4): a story instance of the main world level. */
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

} // namespace

int main() {
    std::printf("the membership of a story instance (protocol notes 4.67) with the messages of a backend of the test fixtures\n");
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
    expect(host.gameModes.set({map_of("Test_Story", "newworld_vitaeeterna", kStoryStart)}), "setup: the game modes (a story instance of the main world level)");
    const bt::Player alpha = bt::add_player(host.store, bt::kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    bt::FakeClient a(27000);
    host.enter(a, alpha.registration());
    host.datagram(a.peer, a.report_at(1, kHome.x, kHome.y, kHome.z));
    host.steps(3, {&a});

    swc::net::RepClient::Options options;
    options.token.characterId = kCharacterA;
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
    (void)events();
    const auto& own = client.own();
    expect(client.phase() == swc::net::RepClient::Phase::inGame, "the world entry: InGame");
    expect(own.gameModes.empty() && own.gameModeRevision == 0, "the world entry: no game mode and no GameModeParticipant fragment before the first entry");
    expect(!client.leave_game_mode(), "the world entry: no leave request with no game mode");

    // The entry: the backend sends the membership with no flag (stage QG9e of the backend: PlayerCanQuit shows the extra mode button of
    // the escape menu, which needs a GameMode entity of the client; the leave of the game menu is 926 with the PlayerArena state).
    const backend::world::Entity& entity = *host.world.find_character(kCharacterA);
    expect(host.world.instances().enter(kCharacterA, "Test_Story").error == backend::world::InstanceError::none, "setup: the entry of the story instance");
    host.steps(3, {&a});
    feed();
    const std::string entry = events();
    expect(client.phase() == swc::net::RepClient::Phase::inGame && client.level_changes() == 1, "the entry: InGame after one level change");
    const auto& membership = entity.gameModeMembership;
    expect(membership.has_value() && own.gameModes.size() == 1, "the entry: one active game mode");
    if (membership && own.gameModes.size() == 1) {
        const swc::net::GameModeEntry& mode = own.gameModes.front();
        expect(mode.actor == membership->actor && mode.group == membership->group && mode.facet == membership->facet,
               "the entry: the key A G F of the game mode entity of the backend");
        expect(mode.mode == membership->modeCrc && mode.map == membership->mapCrc && mode.enteredNs == membership->enteredNs && mode.extra == 0,
               "the entry: the CRCs of the mode and of the map, the entry time, X 0");
    }
    expect(own.gameModeStamp != 0, "the entry: the stamp of the replacement");
    expect(!swc::net::has_game_mode_flag(own.gameModeFlags, swc::net::kGameModeFlagCanQuit)
               && own.gameModeFlags == swc::net::GameModeFlags{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
           "the entry: no flag (backend stage QG9e)");
    expect(entry.find("game mode (3312): 1 active") != std::string::npos, "the entry: the log line of the state");

    // The result 3094 of an entrance (the values of the live run L1 of stage QG9a): the client keeps it for DungeonEnterScreen.
    host.world.dungeon_result(entity.owner, 0x53b85d6577faef54ULL, 0xd95c21d3f6dcde53ULL, swc::net::kDungeonResultEntranceBusy);
    host.steps(1, {&a});
    feed();
    expect(own.dungeonResults == 1 && own.lastDungeonResult.result == swc::net::kDungeonResultEntranceBusy
               && own.lastDungeonResult.target == 0x53b85d6577faef54ULL && own.lastDungeonResult.entity == 0xd95c21d3f6dcde53ULL,
           "3094: the count, the result 17, the target and G of the entrance");
    expect(events().find("(3094): 17, entrance d95c21d3f6dcde53") != std::string::npos, "3094: the log line");
    swc::net::Bytes shortBody = backend::server::rep::dungeon_result_body(1, 2, 0);
    shortBody.pop_back();
    expect(!swc::net::read_dungeon_result(shortBody) && swc::net::read_dungeon_result(backend::server::rep::dungeon_result_body(1, 2, 0)),
           "3094: a body of 19 bytes does not have the form, 20 bytes have it");

    // The leave 3309 with a key that is not the key of the membership: the backend changes nothing.
    const std::uint32_t revision = own.gameModeRevision;
    swc::net::GameModeEntry other = own.gameModes.front();
    other.facet += 1;
    host.datagram(a.peer, a.component_message(backend::server::rep::kTypeRequestLeaveGameMode, entity.actor.bytes,
                                              swc::net::leave_game_mode_body(own.group, other)));
    host.steps(2, {&a});
    expect(logged("names a game mode that is not the active one") == 1 && host.world.instances().instance_of(kCharacterA) != nullptr,
           "3309 with an other key: the backend reads the body and changes nothing");

    // The leave 3309 of this client: the key of the first entry. RepClient::leave_game_mode sends it; its bytes go to the backend here.
    expect(client.leave_game_mode() && events().find("the leave request (3309)") != std::string::npos, "3309: RepClient sends the leave");
    const swc::net::Bytes leave = swc::net::leave_game_mode_body(own.group, own.gameModes.front());
    expect(leave.size() == 48, "3309: 48 bytes (target, entity, A, G, F)");
    host.datagram(a.peer, a.component_message(backend::server::rep::kTypeRequestLeaveGameMode, entity.actor.bytes, leave));
    host.steps(3, {&a});
    expect(host.world.instances().instance_of(kCharacterA) == nullptr, "3309: the backend reads the body and the character leaves the instance");
    feed();
    (void)events();
    expect(client.phase() == swc::net::RepClient::Phase::inGame && client.level_changes() == 2, "3309: the level change to the main world");
    expect(own.gameModes.empty() && !swc::net::has_game_mode_flag(own.gameModeFlags, swc::net::kGameModeFlagCanQuit) && own.gameModeRevision > revision,
           "3309: the empty map and no flag after the leave");

    // The second entry, and the leave event 1980 of the escape menu.
    expect(host.world.instances().enter(kCharacterA, "Test_Story").error == backend::world::InstanceError::none, "setup: the second entry");
    host.steps(3, {&a});
    feed();
    (void)events();
    expect(own.gameModes.size() == 1 && client.level_changes() == 3, "the second entry: one active game mode again");
    host.datagram(a.peer, a.component_message(backend::server::rep::kTypeGameModeClientEvent, entity.actor.bytes,
                                              swc::net::game_mode_event_body(own.group, 0x12345678U)));
    host.steps(2, {&a});
    expect(logged("the client event 12345678 of a game mode") == 1 && host.world.instances().instance_of(kCharacterA) != nullptr,
           "1980 with an other event: the backend reads the body and changes nothing");
    client.game_mode_event();
    expect(events().find("the client event 9bb080d0 (1980)") != std::string::npos, "1980: RepClient sends the leave event");
    // An interaction request of the old context (the block of the exit portal): the level change ends it (window 1 of C-13 step 1b).
    swc::net::InteractionRequest portal;
    portal.action = swc::net::kActionExitDungeon;
    portal.optionId = 0xD5D62681U;
    client.set_interaction(portal);
    const swc::net::Bytes event = swc::net::game_mode_event_body(own.group, swc::net::kGameModeLeaveEvent);
    expect(event.size() == 20, "1980: 20 bytes (target, entity, event)");
    host.datagram(a.peer, a.component_message(backend::server::rep::kTypeGameModeClientEvent, entity.actor.bytes, event));
    host.steps(3, {&a});
    expect(host.world.instances().instance_of(kCharacterA) == nullptr, "1980: the backend reads the leave event and the character leaves");
    feed();
    (void)events();
    expect(own.gameModes.empty() && client.level_changes() == 4, "1980: the level change and the empty map");
    expect(!own.interaction.has_value(), "1980: the level change ends the interaction request of the old context");

    // The facet target: G + the GameModeParticipant component of the root entity (slice-ids.json RootPlayer/b5a705af-...).
    expect(swc::net::game_mode_event_body(own.group, 0).size() == 20
               && swc::net::Reader(swc::net::game_mode_event_body(own.group, 0)).u64() == own.group + 0x8DE633590B8FBEE0ULL,
           "the target of 3309 and 1980: G + 8DE633590B8FBEE0");

    // The Enter request of DungeonEnterScreen: action 72, base 71, not committed (the form of the live run: 1589 with 72 base 71).
    swc::net::InteractionRequest enter;
    enter.action = swc::net::kActionEnterDungeon;
    enter.committed = true;
    enter.optionId = 0xB17F828EU;
    const swc::net::InteractionRequest request = swc::net::dungeon_enter_request(enter);
    expect(request.action == 72 && request.baseAction == 71 && !request.committed && request.optionId == 0xB17F828EU,
           "the Enter request: action 72, base action 71, not committed, the option CRC");

    backend::log::set_sink({});
    std::printf("game mode tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
