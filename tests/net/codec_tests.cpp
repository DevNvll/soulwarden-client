// Tests of the network codec of the client (Source/SoulwardenClient/Net/Codec) with bytes that the backend makes: the
// stored datagrams of one session of the backend test program (golden file of case T2), the writers of the made-up
// client of the backend tests, the carrier of the backend, and the input reader of the backend. No capture of the user.
// Build and run: tools/test-net.sh. The Soulwarden sources are read only: the build output is outside the repository.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "Net/Codec/SwcCarrier.h"
#include "Net/Codec/SwcRepClient.h"
#include "Net/Codec/SwcRepMessages.h"
#include "core/logging/log.h"
#include "server/carrier/carrier.h"
#include "server/rep/client_input.h"
#include "support/client_frames.h"

namespace {

using swc::net::Bytes;
using Clock = std::chrono::steady_clock;
namespace backend = soulwarden::backend;

/** The values of the golden scenario (tests/support/golden_scenario.h of the backend, which needs the store). */
constexpr const char* kGoldenCharacterId = "0a0a0a0a-1111-4222-8333-444444444444";
constexpr const char* kGoldenCharacterName = "Golden";

int g_checks = 0;
int g_failures = 0;

void expect(bool condition, const std::string& text) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL %s\n", text.c_str());
    }
}

bool near(float a, float b, float tolerance) {
    return std::fabs(a - b) <= tolerance;
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

swc::net::LoginToken golden_token() {
    swc::net::LoginToken token;
    token.characterId = kGoldenCharacterId;
    token.personaId = "persona";
    token.steamUserId = backend::tests::kSteamIdA;
    return token;
}

/** The golden session: the datagrams of the server in sequence, each one after a step of 10 ms. */
void test_golden_session(const std::string& goldenPath) {
    std::printf("golden session (case T2 of the backend tests)\n");
    const std::vector<Bytes> datagrams = read_golden(goldenPath);
    expect(datagrams.size() > 10, "the golden file has the datagrams of the session");
    swc::net::RepClient::Options options;
    options.token = golden_token();
    options.steamId = backend::tests::kSteamIdA;
    options.ticketHex = swc::net::local_ticket_hex(backend::tests::kSteamIdA, "Tester");
    swc::net::RepClient client(options);
    Clock::time_point now{};
    client.start(now);
    // A carrier of the backend reads each datagram of the client: the registration and the input reports.
    std::vector<std::pair<std::uint8_t, Bytes>> serverGot;
    backend::server::carrier::Connection serverView(
        [](std::span<const std::uint8_t>) {},
        [&](std::uint8_t channel, std::span<const std::uint8_t> data) { serverGot.emplace_back(channel, Bytes(data.begin(), data.end())); });
    std::size_t largest = 0;
    std::size_t largestOut = 0;
    for (const Bytes& datagram : datagrams) {
        largest = std::max(largest, datagram.size());
        now += std::chrono::milliseconds(10);
        client.receive(datagram, now);
        for (const Bytes& out : client.tick(now)) {
            largestOut = std::max(largestOut, out.size());
            serverView.receive(out);
        }
    }
    for (const auto& event : client.take_events()) {
        std::printf("    %s\n", event.text.c_str());
    }
    expect(largest <= swc::net::kMaxDatagram && largestOut <= swc::net::kMaxDatagram, "each datagram has 1,038 bytes at most");
    expect(client.phase() == swc::net::RepClient::Phase::inGame, "the client is in the world");
    const auto& own = client.own();
    expect(own.characterId == kGoldenCharacterId, "the own entity has the character id of the login");
    expect(own.name == kGoldenCharacterName, "the own entity has the name of the character");
    expect(own.connected, "playerConnected is 1");
    expect(swc::net::hex_of(own.actor) == "02000000534f554c10000000534f554c", "the actor id of the own entity (serial 1, kind 10)");
    expect(own.group == 0x4C554F5300000002ULL, "G of the own entity");
    expect(swc::net::hex_of(own.sliceAsset) == "a660eeebebc75cb7be6bab11eb831731" && own.sliceSubId == 2, "the player slice asset, sub id 2");
    expect(own.hasPosition && near(own.position.x, 100.0F, 0.001F) && near(own.position.y, 200.0F, 0.001F), "the spawn position x y");
    expect(near(own.position.z, 50.5F, 0.02F), "the spawn height: the saved height plus the lift of 0.5 m");
    expect(near(own.rotation.w, 1.0F, 0.001F) && near(own.lookDirection.y, 1.0F, 0.001F), "rotation 0x6E and look direction 0x3A");
    expect(own.dataBits == 0xC0 && own.moreDataBits == 0x03, "the movement permissions C0 03");
    expect(own.stateId == 0x8000, "the heartbeat state has the id 0x8000");
    expect(own.timeAnchor != 0, "a time anchor");
    expect(near(own.healthMax, 1000.0F, 0.01F), "the vitals state: maximum health 1000 with no progression tables");
    expect(own.hasAppearance, "the appearance state");
    expect(own.hasMana && near(own.mana, 100.0F, 0.001F) && near(own.manaMax, 100.0F, 0.001F), "the mana state (slot 47): 100 of 100");
    expect(near(own.vitalsStamina, 100.0F, 0.001F) && near(own.vitalsStaminaMax, 100.0F, 0.001F) && near(own.vitalsMana, 100.0F, 0.001F)
               && near(own.vitalsManaMax, 100.0F, 0.001F),
           "the stamina and mana values of the vitals state");
    expect(!own.dead, "not dead");
    // The server clock of the registration reply: the scenario starts at 1,000,000 s of the server clock.
    expect(client.has_server_clock() && client.server_time_ns(now) >= 1'000'000'000'000'000ULL
               && client.server_time_ns(now) < 1'000'001'000'000'000ULL,
           "the server clock of the registration reply");
    const auto& counts = client.counts();
    expect(counts.incompleteBundles == 0, "each state bundle was read to its last byte");
    expect(counts.droppedBundles == 0, "no state bundle was dropped");
    expect(counts.bundles >= 4, "the first bundle, the own record, and the heartbeat states");
    expect(counts.movementStates >= 3, "the movement states (the spawn and the heartbeats)");
    expect(client.self_identification() && swc::net::hex_of(client.self_identification()->sessionActor) == "02000020534f554c02000000534f554c",
           "the session actor of the self identification");
    expect(client.level_info() && client.level_info()->level == "newworld_vitaeeterna" && client.level_info()->instance == 1
               && client.level_info()->reload && client.level_info()->version == 1,
           "the level information");
    expect(counts.inputReports > 0, "input reports after InGame");
    std::size_t registrations = 0;
    std::size_t reports = 0;
    for (const auto& [channel, frame] : serverGot) {
        std::span<const std::uint8_t> payload;
        if (!backend::server::hub::decode_frame(frame, payload) || payload.size() < 19) {
            expect(false, "a client frame with its checksum");
            continue;
        }
        backend::server::hub::Reader reader(payload.subspan(16));
        reader.u8();
        reader.u8();
        const std::uint32_t type = reader.size();
        const auto body = payload.subspan(16 + reader.position());
        if (type == 19 && channel == 0) {
            ++registrations;
        } else if (type == 1589 && channel == 1) {
            ++reports;
            const auto input = backend::server::rep::read_client_input(body);
            expect(input && input->hasCamera && near(input->cameraX, 100.5F, 0.001F) && near(input->cameraY, 196.0F, 0.001F)
                       && near(input->cameraZ, 52.14F, 0.02F) && !input->hasCharacter,
                   "the backend reads the input report: the camera of a character at rest at the spawn, no position field");
            expect(std::equal(payload.begin(), payload.begin() + 16, client.own().actor.begin()), "the route of the report: the own actor id");
        }
    }
    expect(registrations == 1, "the backend carrier got one registration request on channel 0");
    expect(reports == counts.inputReports, "the backend carrier got each input report on channel 1");

    // The 2 s timer: no movement state for 2 s.
    for (int step = 0; step < 300; ++step) {
        now += std::chrono::milliseconds(10);
        client.tick(now);
    }
    expect(client.movement_stale(), "2 s with no movement state: the input has no actions");
    // The 30 s timer: no game message for 30 s.
    for (int step = 0; step < 3000; ++step) {
        now += std::chrono::milliseconds(10);
        client.tick(now);
    }
    expect(client.phase() == swc::net::RepClient::Phase::ended && client.end_cause().find("connection_ingame_timeout") == 0,
           "30 s with no game message: the client ends the connection");
}

/** The client writers against the writers of the made-up client of the backend tests (tests/support/client_frames.h). */
void test_client_writers() {
    std::printf("client writers against the made-up client of the backend tests\n");
    backend::tests::Registration fields;
    fields.characterId = "0a0a0a0a-1111-4222-8333-444444444444";
    fields.personaId = "11111111-0000-4000-8000-000000000001";
    fields.steamId = backend::tests::kSteamIdA;
    fields.personaName = "Tester \xC3\x89";
    swc::net::LoginToken token;
    token.signature = "signature";
    token.repAddress = "127.0.0.1:32210";
    token.worldId = backend::tests::kWorldId;
    token.characterId = fields.characterId;
    token.personaId = fields.personaId;
    token.steamUserId = fields.steamId;
    token.channelId = "channel";
    token.ticketId = fields.ticketId;
    token.issueTime = 100;
    token.generateTime = 100;
    token.accountAge = 0;
    token.tokenVersion = 10;
    const std::string ticket = swc::net::local_ticket_hex(fields.steamId, fields.personaName);
    expect("steam|" + ticket == backend::tests::local_ticket_text(fields.steamId, fields.personaName), "the local ticket");
    const Bytes ours = swc::net::registration_body(token, fields.steamId, ticket);
    expect(ours == backend::tests::registration_body(fields), "the registration request body, byte for byte");

    swc::net::ActorId route{};
    route.fill(0xB1);
    const std::uint8_t body[4] = {1, 2, 3, 4};
    expect(swc::net::client_frame(route, 19, body) == backend::tests::client_frame(19, body), "the client frame (crc32 size route envelope)");

    // The entry of a client with the position report, and an entry with a camera and a look direction.
    const auto ours_of = [](const backend::tests::InputEntry& entry) {
        swc::net::InputEntry out;
        out.sequence = entry.sequence;
        out.flags = entry.flags;
        out.permissions = entry.permissions;
        out.groundDistance = entry.groundDistance;
        out.actionWords = entry.actionWords;
        out.actionBits = entry.actionBits;
        out.stepTime = entry.stepTime;
        out.hasLook = entry.hasLook;
        out.yaw = entry.yaw;
        out.pitch = entry.pitch;
        out.hasCamera = entry.hasCamera;
        out.camera = {entry.cameraX, entry.cameraY, entry.cameraZ};
        out.hasCharacter = entry.hasCharacter;
        out.character = {entry.characterX, entry.characterY, entry.characterZ};
        out.value470 = entry.value470;
        return out;
    };
    backend::tests::InputEntry report = backend::tests::report_entry(7, 8880.0F, 4250.008F, 138.217F);
    report.actionBits = {475, 497, 506, 569};
    backend::tests::InputEntry older = report;
    older.sequence = 6;
    older.flags = backend::tests::kInputFlagsWorld;
    older.hasCharacter = false;
    backend::tests::InputEntry camera;
    camera.sequence = 5;
    camera.yaw = 0.25F;
    camera.pitch = -0.1F;
    camera.cameraX = 100.5F;
    camera.cameraY = 196.0F;
    camera.cameraZ = 51.65F;
    const std::vector<backend::tests::InputEntry> entries{report, older, camera};
    const Bytes expected = backend::tests::input_entries_body(entries, 0x90A8748FA3C2AD77ULL, 0x544F5A4100000002ULL);
    const Bytes written = swc::net::input_report_body({ours_of(report), ours_of(older), ours_of(camera)}, 0x90A8748FA3C2AD77ULL,
                                                      0x544F5A4100000002ULL);
    expect(written == expected, "the input report body with three entries, byte for byte");

    // The reader of the backend reads the report of this client: the camera of the newest entry, the position, the steps.
    const auto input = backend::server::rep::read_client_input(written);
    expect(input.has_value(), "the backend reads the input report");
    if (input) {
        expect(input->sequence == 7 && input->entries == 3, "the input number and the entry count");
        expect(input->hasCamera && near(input->cameraX, 8880.5F, 0.001F) && near(input->cameraY, 4246.008F, 0.001F), "the camera");
        expect(input->hasCharacter && near(input->characterX, 8880.0F, 0.001F) && near(input->characterZ, 138.217F, 0.02F),
               "the position of the character");
        expect(input->steps.size() == 3, "the backend reads each entry to its end");
    }
    // The target of the action list facet: G + the component id (research ids.md: 90A8748FA3C2AD77 for serial 1 of "AZOT").
    swc::net::ActorId actor{0x02, 0x00, 0x00, 0x00, 0x41, 0x5A, 0x4F, 0x54, 0x10, 0x00, 0x00, 0x00, 0x41, 0x5A, 0x4F, 0x54};
    expect(swc::net::group_of(actor) + swc::net::kActionListComponent == 0x90A8748FA3C2AD77ULL, "the facet target of the action list");
}

/** The carrier of this client against the carrier of the backend (apps/backend/src/server/carrier). */
void test_carrier() {
    std::printf("carrier against the carrier of the backend\n");
    std::vector<Bytes> toClient;
    std::vector<std::pair<std::uint8_t, Bytes>> serverGot;
    backend::server::carrier::Connection server(
        [&](std::span<const std::uint8_t> datagram) { toClient.emplace_back(datagram.begin(), datagram.end()); },
        [&](std::uint8_t channel, std::span<const std::uint8_t> data) { serverGot.emplace_back(channel, Bytes(data.begin(), data.end())); });
    swc::net::Carrier client;
    Clock::time_point now{};
    std::size_t largest = 0;
    std::function<bool(const Bytes&)> dropToClient = [](const Bytes&) { return false; };
    const auto exchange = [&](int steps) {
        for (int step = 0; step < steps; ++step) {
            now += std::chrono::milliseconds(10);
            for (const Bytes& datagram : client.tick(now)) {
                largest = std::max(largest, datagram.size());
                server.receive(datagram);
            }
            server.tick(now);
            std::vector<Bytes> datagrams;
            datagrams.swap(toClient);
            for (const Bytes& datagram : datagrams) {
                largest = std::max(largest, datagram.size());
                if (!dropToClient(datagram)) {
                    client.receive(datagram);
                }
            }
        }
    };
    client.connect();
    exchange(5);
    expect(client.state() == swc::net::Carrier::State::connected, "the handshake: the client got the connect ack");
    expect(server.state() == backend::server::carrier::State::connected, "the handshake: the server took the connect request");

    Bytes big(3000);
    for (std::size_t index = 0; index < big.size(); ++index) {
        big[index] = static_cast<std::uint8_t>(index * 7);
    }
    client.send(swc::net::kChannelReliable, big, true);
    const std::uint8_t small[3] = {9, 8, 7};
    client.send(swc::net::kChannelUnreliable, small, false);
    exchange(5);
    expect(serverGot.size() == 2 && serverGot[0].first == 0 && serverGot[0].second == big, "a reliable message of 3,000 bytes in chunks");
    expect(serverGot.size() == 2 && serverGot[1].first == 1 && serverGot[1].second == Bytes(small, small + 3), "an unreliable message");

    Bytes reply(2500);
    for (std::size_t index = 0; index < reply.size(); ++index) {
        reply[index] = static_cast<std::uint8_t>(index * 13 + 1);
    }
    server.send(0, reply, backend::server::carrier::Reliability::reliable);
    exchange(5);
    auto delivered = client.take_delivered();
    expect(delivered.size() == 1 && delivered[0].channel == 0 && delivered[0].data == reply, "a reliable message of the server in chunks");

    // A lost datagram of the server: the server sends it again after 250 ms, and the order stays.
    const std::uint8_t first[2] = {1, 1};
    const std::uint8_t second[2] = {2, 2};
    bool dropped = false;
    dropToClient = [&](const Bytes& datagram) {
        if (!dropped && datagram.size() > 9) {
            dropped = true;
            return true;
        }
        return false;
    };
    server.send(0, first, backend::server::carrier::Reliability::reliable);
    exchange(1);
    server.send(0, second, backend::server::carrier::Reliability::reliable);
    exchange(40);
    delivered = client.take_delivered();
    expect(dropped, "one datagram was lost");
    expect(delivered.size() == 2 && delivered[0].data == Bytes(first, first + 2) && delivered[1].data == Bytes(second, second + 2),
           "the lost message came again, in its order");
    expect(client.counts().datagramsIn > 0 && largest <= swc::net::kMaxDatagram, "each datagram has 1,038 bytes at most");

    client.disconnect();
    exchange(2);
    expect(server.state() == backend::server::carrier::State::disconnected, "the carrier disconnect of the client");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: swc-net-tests GOLDEN_FILE\n");
        return 2;
    }
    backend::log::set_sink([](const std::string&) {});
    test_golden_session(argv[1]);
    test_client_writers();
    test_carrier();
    std::printf("Result: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
