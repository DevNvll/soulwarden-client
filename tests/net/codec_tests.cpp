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
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Net/Codec/SwcCarrier.h"
#include "Net/Codec/SwcRepClient.h"
#include "Net/Codec/SwcRepMessages.h"
#include "Interact/SwcConversation.h"
#include "Interact/SwcInteraction.h"
#include "core/logging/log.h"
#include "server/carrier/carrier.h"
#include "server/rep/client_input.h"
#include "server/rep/health_messages.h"
#include "server/rep/item_requests.h"
#include "server/rep/lock_messages.h"
#include "server/rep/travel_messages.h"
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
    // Stage P3.5: the shot of a cast comes from the input step of the player (the attack driver puts its attack there).
    std::optional<swc::net::ShotRequest> pendingShot;
    options.inputStep = [&pendingShot](std::uint16_t, std::int64_t, std::vector<unsigned>&, std::vector<swc::net::ShotRequest>& shots) {
        if (pendingShot) {
            shots.push_back(*pendingShot);
            pendingShot.reset();
        }
    };
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
    // The first movement state of the session has the teleport number 1: the client takes it (the world entry is a travel).
    expect((own.teleportId & 0x0F) == 1 && own.travelRevision == 1, "the world entry: teleport number 1, one travel");
    expect(!own.loadingScreen && own.deathNotices == 0 && !own.dead, "no loading screen, no death notice, the character lives");
    expect(near(own.healthMax, 1000.0F, 0.01F), "the vitals state: maximum health 1000 with no progression tables");
    expect(own.hasAppearance, "the appearance state");
    expect(own.hasMana && near(own.mana, 100.0F, 0.001F) && near(own.manaMax, 100.0F, 0.001F), "the mana state (slot 47): 100 of 100");
    expect(near(own.vitalsStamina, 100.0F, 0.001F) && near(own.vitalsStaminaMax, 100.0F, 0.001F) && near(own.vitalsMana, 100.0F, 0.001F)
               && near(own.vitalsManaMax, 100.0F, 0.001F),
           "the stamina and mana values of the vitals state");
    expect(!own.dead, "not dead");
    // The ProgressionPoint state (slot 67, type 3681; stage P3.5): the reader of the backend tests reads it, and the character of the
    // scenario has no node (the dev runs check a character with the nodes of the build: docs/ACTIONS.md).
    expect(own.nodesRevision >= 1 && own.learnedNodes.empty(), "the ProgressionPoint state: the weapon trees of the character, no node");
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

    // The player of the game thread (stage 1.7): the position field, the camera, the look, and the forward key.
    swc::net::RepClient::Motion motion;
    motion.character = {101.25F, 203.5F, 50.0F};
    motion.camera = {101.75F, 199.5F, 51.7F};
    motion.yaw = 0.5F;
    motion.pitch = -0.25F;
    motion.forward = true;
    client.set_motion(motion);
    serverGot.clear();
    for (int step = 0; step < 4; ++step) {
        now += std::chrono::milliseconds(34);
        for (const Bytes& out : client.tick(now)) {
            serverView.receive(out);
        }
    }
    std::optional<backend::server::rep::ClientInput> moved;
    for (const auto& [channel, frame] : serverGot) {
        std::span<const std::uint8_t> payload;
        if (channel != 1 || !backend::server::hub::decode_frame(frame, payload) || payload.size() < 19) {
            continue;
        }
        backend::server::hub::Reader reader(payload.subspan(16));
        reader.u8();
        reader.u8();
        if (reader.size() == 1589) {
            moved = backend::server::rep::read_client_input(payload.subspan(16 + reader.position()));
        }
    }
    expect(moved.has_value(), "input reports with the motion of the player");
    if (moved) {
        expect((moved->flags & backend::server::rep::kInputFlagPosition) != 0 && moved->hasCharacter && near(moved->characterX, 101.25F, 0.001F)
                   && near(moved->characterY, 203.5F, 0.001F) && near(moved->characterZ, 50.0F, 0.02F),
               "the backend reads the position field (flag 0x8000) of the character");
        expect(moved->hasCamera && near(moved->cameraX, 101.75F, 0.001F) && near(moved->cameraY, 199.5F, 0.001F) && near(moved->cameraZ, 51.7F, 0.02F),
               "the backend reads the camera of the player");
        expect(moved->hasLook && near(moved->lookYaw, 0.5F, 0.0001F) && near(moved->lookPitch, -0.25F, 0.0001F), "the backend reads the look");
        expect(moved->forward, "the backend reads the forward key (action bit 14)");
        expect(moved->entries == 5 && moved->steps.size() == 5, "the backend reads the 5 entries of the report to their end");
    }
    // Stage WF5: a swimmer has the depth class byte (mask bit 3; the retail client: 2 in a swim, set3-swim/swim-wire.json) and the
    // water below its feet (mask bit 4). The reader of the backend reads them and the fields after them.
    motion.depthClass = 2;
    motion.airHeight = 1.25F;
    client.set_motion(motion);
    serverGot.clear();
    for (int step = 0; step < 4; ++step) {
        now += std::chrono::milliseconds(34);
        for (const Bytes& out : client.tick(now)) {
            serverView.receive(out);
        }
    }
    std::optional<backend::server::rep::ClientInput> swimming;
    for (const auto& [channel, frame] : serverGot) {
        std::span<const std::uint8_t> payload;
        if (channel != 1 || !backend::server::hub::decode_frame(frame, payload) || payload.size() < 19) {
            continue;
        }
        backend::server::hub::Reader reader(payload.subspan(16));
        reader.u8();
        reader.u8();
        if (reader.size() == 1589) {
            swimming = backend::server::rep::read_client_input(payload.subspan(16 + reader.position()));
        }
    }
    expect(swimming && swimming->hasCharacter && near(swimming->characterX, 101.25F, 0.001F) && swimming->hasLook && swimming->forward
               && swimming->entries == 5 && swimming->steps.size() == 5 && swimming->tail == backend::server::rep::InputTail::read,
           "the depth class byte (mask bit 3) and the ground distance: the backend reads each entry to its end");
    motion.depthClass = 0;
    motion.airHeight = -1.0F;
    client.set_motion(motion);
    // A long frame of the game thread (the world level check): the character moved 0.66 m between two steps. The position
    // field goes to it at 10 m/s at most (0.333 m in one step), under the 20 m/s of the backend for one step; a travel of
    // more than 20 m goes at once.
    const auto newest_x = [&]() -> std::optional<float> {
        std::optional<float> x;
        for (const auto& [channel, frame] : serverGot) {
            std::span<const std::uint8_t> payload;
            if (channel != 1 || !backend::server::hub::decode_frame(frame, payload) || payload.size() < 19) {
                continue;
            }
            backend::server::hub::Reader reader(payload.subspan(16));
            reader.u8();
            reader.u8();
            if (reader.size() == 1589) {
                if (const auto input = backend::server::rep::read_client_input(payload.subspan(16 + reader.position())); input && input->hasCharacter) {
                    x = input->characterX;
                }
            }
        }
        return x;
    };
    const auto one_step = [&]() -> std::optional<float> {
        serverGot.clear();
        now += std::chrono::milliseconds(34);
        for (const Bytes& out : client.tick(now)) {
            serverView.receive(out);
        }
        return newest_x();
    };
    motion.character.x = 101.91F;
    client.set_motion(motion);
    const std::optional<float> first = one_step();
    const std::optional<float> second = one_step();
    const std::optional<float> third = one_step();
    expect(first && second && third && near(*first, 101.25F + 1.0F / 3.0F, 0.002F) && near(*second, 101.91F, 0.002F) && near(*third, 101.91F, 0.002F),
           "a move of 0.66 m in one frame: the position field goes 0.333 m in the first step, then to the character");
    motion.character.x = 141.91F;
    client.set_motion(motion);
    const std::optional<float> travel = one_step();
    expect(travel && near(*travel, 141.91F, 0.002F), "a travel of 40 m: the position field goes at once");
    // Two melee contacts (stage P2.8): list 3 of the next entry only; the reader of the backend reads each field.
    swc::net::MeleeContactItem first_contact;
    for (std::size_t index = 0; index < 16; ++index) {
        first_contact.groupActor[index] = static_cast<std::uint8_t>(0x10 + index);
        first_contact.actor[index] = static_cast<std::uint8_t>(0x30 + index);
    }
    first_contact.group = 0x1122334455667788ULL;
    first_contact.entity = 0x1122334455667799ULL;
    first_contact.facet = 0x0102030405060708ULL;
    first_contact.point = {141.5F, 203.25F, 51.0F};
    first_contact.normal = {0.0F, -1.0F, 0.0F};
    first_contact.direction = {0.6F, 0.8F, 0.0F};
    swc::net::MeleeContactItem second_contact = first_contact;
    second_contact.facet = 0x0A0B0C0D0E0F1011ULL;
    second_contact.provider = 7;
    client.queue_contacts({first_contact, second_contact});
    std::vector<backend::server::rep::InputContact> read;
    std::size_t entriesWithContacts = 0;
    for (int step = 0; step < 3; ++step) {
        serverGot.clear();
        now += std::chrono::milliseconds(34);
        for (const Bytes& out : client.tick(now)) {
            serverView.receive(out);
        }
        for (const auto& [channel, frame] : serverGot) {
            std::span<const std::uint8_t> payload;
            if (channel != 1 || !backend::server::hub::decode_frame(frame, payload) || payload.size() < 19) {
                continue;
            }
            backend::server::hub::Reader reader(payload.subspan(16));
            reader.u8();
            reader.u8();
            if (reader.size() != 1589) {
                continue;
            }
            if (const auto input = backend::server::rep::read_client_input(payload.subspan(16 + reader.position())); input && !input->steps.empty()) {
                // The newest entry first: its own items only.
                if (!input->steps.front().contacts.empty()) {
                    ++entriesWithContacts;
                    read = input->steps.front().contacts;
                }
                expect(input->tail == backend::server::rep::InputTail::read && input->entries == input->steps.size(),
                       "the backend reads each entry of a report with contacts");
            }
        }
    }
    expect(entriesWithContacts == 1 && read.size() == 2, "the two contacts are in list 3 of one entry (" + std::to_string(entriesWithContacts) + " entries)");
    if (read.size() == 2) {
        const auto& a = read[0];
        const auto& b = read[1];
        expect(a.provider == 0xFFFFFFFFU && b.provider == 7, "the provider of each contact");
        expect(std::equal(a.groupActor.begin(), a.groupActor.end(), first_contact.groupActor.begin()) && a.group == first_contact.group
                   && std::equal(a.actor.begin(), a.actor.end(), first_contact.actor.begin()) && a.entity == first_contact.entity,
               "the group and the receiver references of a contact");
        expect(a.facet == first_contact.facet && b.facet == second_contact.facet, "the facet target of each contact");
        expect(near(a.point[0], 141.5F, 0.0001F) && near(a.point[1], 203.25F, 0.0001F) && near(a.point[2], 51.0F, 0.0001F)
                   && near(a.normal[1], -1.0F, 0.0001F) && near(a.direction[0], 0.6F, 0.0001F) && near(a.direction[1], 0.8F, 0.0001F),
               "the point, the normal, and the direction of a contact");
    }
    expect(client.counts().meleeContacts == 2, "the count of the contacts that went out");
    // The shot of Gravity Well (stage P3.5; protocol notes 4.27): one item of list 2 of one entry, the origin 1.5 m above the
    // character, the direction of the look, the attack 8E14366B, no receiver; the reader of the backend reads each field.
    pendingShot = swc::net::ShotRequest{0x8E14366BU, 0, 0.0F, 0, 0.0F, 1.5F};
    std::vector<backend::server::rep::InputShot> shotsRead;
    std::size_t entriesWithShots = 0;
    const auto read_shots = [&]() {
    for (int step = 0; step < 3; ++step) {
        serverGot.clear();
        now += std::chrono::milliseconds(34);
        for (const Bytes& out : client.tick(now)) {
            serverView.receive(out);
        }
        for (const auto& [channel, frame] : serverGot) {
            std::span<const std::uint8_t> payload;
            if (channel != 1 || !backend::server::hub::decode_frame(frame, payload) || payload.size() < 19) {
                continue;
            }
            backend::server::hub::Reader reader(payload.subspan(16));
            reader.u8();
            reader.u8();
            if (reader.size() != 1589) {
                continue;
            }
            if (const auto input = backend::server::rep::read_client_input(payload.subspan(16 + reader.position())); input && !input->steps.empty()) {
                if (!input->steps.front().shotItems.empty()) {
                    ++entriesWithShots;
                    shotsRead = input->steps.front().shotItems;
                }
                expect(input->tail == backend::server::rep::InputTail::read && input->entries == input->steps.size()
                           && input->hasCharacter && near(input->characterX, 141.91F, 0.002F),
                       "the backend reads each entry of a report with a shot, to the position field after the lists");
            }
        }
    }
    };
    read_shots();
    expect(entriesWithShots == 1 && shotsRead.size() == 1, "the shot is in list 2 of one entry (" + std::to_string(entriesWithShots) + " entries)");
    if (shotsRead.size() == 1) {
        const backend::server::rep::InputShot& shot = shotsRead[0];
        expect(near(shot.origin[0], 141.91F, 0.002F) && near(shot.origin[1], 203.5F, 0.002F) && near(shot.origin[2], 51.5F, 0.02F),
               "the origin of the shot: the character, 1.5 m higher");
        expect(near(shot.direction[0], -std::sin(0.5F) * std::cos(0.25F), 0.0001F) && near(shot.direction[1], std::cos(0.5F) * std::cos(0.25F), 0.0001F)
                   && near(shot.direction[2], std::sin(0.25F), 0.0001F),
               "the direction of the shot: the look (yaw 0.5 to the left of north, pitch 0.25 up)");
        expect(shot.attack == 0x8E14366BU && !shot.hasReceiver && shot.charge == 0.0F && shot.ammo == 0 && shot.row == 0,
               "the attack of the shot (gravitywell), no receiver, no charge, no ammunition, no row");
    }
    expect(client.counts().shots == 1, "the count of the shots that went out");
    // A charged arrow (stage P3.6; protocol notes 4.27): the origin 1.0 m along the look and 1.46 m above the feet, the direction to the
    // aim point of the camera, the attack basic_heavy, the row bowattack2, the charge 100, the ammunition DefaultArrow.
    motion.hasAimPoint = true;
    motion.aimPoint = {130.0F, 240.0F, 55.0F};
    client.set_motion(motion);
    pendingShot = swc::net::ShotRequest{0xA005D4C3U, 0x252222BDU, 100.0F, 0xC0E3A523U, 1.0F, 1.46F};
    entriesWithShots = 0;
    shotsRead.clear();
    read_shots();
    expect(entriesWithShots == 1 && shotsRead.size() == 1, "the arrow is in list 2 of one entry");
    if (shotsRead.size() == 1) {
        const backend::server::rep::InputShot& shot = shotsRead[0];
        const float originX = 141.91F - std::sin(0.5F) * 1.0F;
        const float originY = 203.5F + std::cos(0.5F) * 1.0F;
        expect(near(shot.origin[0], originX, 0.002F) && near(shot.origin[1], originY, 0.002F) && near(shot.origin[2], 51.46F, 0.02F),
               "the origin of the arrow: 1.0 m along the look, 1.46 m above the feet");
        const float dx = 130.0F - shot.origin[0];
        const float dy = 240.0F - shot.origin[1];
        const float dz = 55.0F - shot.origin[2];
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
        expect(near(shot.direction[0], dx / length, 0.0005F) && near(shot.direction[1], dy / length, 0.0005F) && near(shot.direction[2], dz / length, 0.0005F),
               "the direction of the arrow: to the aim point (a unit vector)");
        expect(shot.attack == 0xA005D4C3U && shot.row == 0x252222BDU && shot.charge == 100.0F && shot.ammo == 0xC0E3A523U && !shot.hasReceiver,
               "the arrow: basic_heavy, bowattack2, charge 100, DefaultArrow, no receiver");
    }
    motion.hasAimPoint = false;
    client.set_motion(std::nullopt);

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

/**
 * The life, the travel, and the interactions (stage P2.10): the bodies of the backend writers (server/rep/health_messages,
 * travel_messages, lock_messages) to the readers of this client; the bodies of this client to the readers of the backend; the
 * interaction block of the input entries to the input reader of the backend and to the writer of its made-up client.
 */
void test_life_messages() {
    std::printf("the life, the travel, and the interactions against the writers and the readers of the backend\n");
    namespace rep = backend::server::rep;
    // A character of serial 1 of the backend: G = 4C554F5300000002 (world/ids.h).
    const std::uint64_t group = 0x4C554F5300000002ULL;

    const Bytes death = rep::player_death_body(group, 5.0F);
    const auto readDeath = swc::net::read_player_death(death);
    expect(death.size() == 20 && readDeath.has_value(), "3950: 20 bytes, the client reads them");
    if (readDeath) {
        expect(readDeath->call.entity == group && readDeath->call.target == group + swc::net::kPlayerComponent, "3950: the entity and the target");
        expect(readDeath->time == 5.0F, "3950: the time to the respawn screen");
    }
    // The damage to the own character (3601, stage WF7): 64 slash from a creature at a place, 2.9 % of the health.
    backend::world::DamageTaken taken;
    taken.source = 0x4C554F5300000009ULL;
    taken.from.x = 8880.5;
    taken.from.y = 4250.25;
    taken.from.z = 101.0;
    taken.type = 4;
    taken.amount = 64.0F;
    taken.part = 0.029F;
    const Bytes damage = rep::damage_taken_body(group, taken);
    const auto readDamage = swc::net::read_damage_taken(damage);
    expect(damage.size() == 52 && readDamage.has_value(), "3601: 52 bytes (one entry), the client reads them");
    if (readDamage) {
        expect(readDamage->call.entity == group && readDamage->call.target == rep::player_vitals_target(group), "3601: the entity and the vitals facet");
        expect(readDamage->source == taken.source && (readDamage->flags & swc::net::DamageTaken::kHasPlace) != 0, "3601: the source and the place flag");
        expect(readDamage->part == 0.029F && readDamage->from[0] == 8880.5F && readDamage->from[1] == 4250.25F && readDamage->from[2] == 101.0F,
               "3601: the part of the health and the place of the source");
        expect(readDamage->entries.size() == 1 && readDamage->entries[0].type == 4 && readDamage->entries[0].amount == 64.0F
                   && readDamage->entries[0].part == 1.0F, "3601: one entry: slash 64, part 1");
    }
    Bytes shortDamage = damage;
    shortDamage.pop_back();
    expect(!swc::net::read_damage_taken(shortDamage).has_value(), "3601: a short body is no message");
    const Bytes screen = rep::teleport_screen_body(group);
    const auto readScreen = swc::net::read_component_call(screen);
    expect(screen.size() == 17 && readScreen && readScreen->entity == group && readScreen->target == group + swc::net::kPlayerComponent,
           "3951: the call of the player facet");
    const Bytes teleport = rep::player_teleport_body(group);
    const auto readTeleport = swc::net::read_component_call(teleport);
    expect(teleport.size() == 16 && readTeleport && readTeleport->target == group + swc::net::kPlayerComponent, "3954: the call of the player facet");
    const Bytes cancel = rep::cancel_interaction_body(group);
    const auto readCancel = swc::net::read_component_call(cancel);
    expect(cancel.size() == 16 && readCancel && readCancel->target == group + swc::net::kInteractorComponent, "3746: the call of the interactor facet");
    const Bytes lock = rep::cutscene_finalized_body(group);
    const auto readLock = swc::net::read_component_call(lock);
    expect(lock.size() == 16 && readLock && readLock->target == group + swc::net::kPlayerCutsceneComponent, "6709: the call of the cutscene facet");
    // Stage P4.8: the end of the cutscene of a conversation (6705): the reader of the backend (rep_session handle_cutscene_ended reads a
    // component call of the PlayerCutscene facet of the character); the same layout as the answer 6709 of the backend.
    const Bytes ended = swc::net::cutscene_ended_body(group);
    const auto readEnded = rep::read_component_call(ended);
    expect(ended.size() == 16 && readEnded && readEnded->target == group + rep::kPlayerCutsceneComponent && readEnded->entity == group && ended == lock
               && swc::net::kCutsceneEnded.index == rep::kTypeCutsceneEnded,
           "6705: the call of the cutscene facet of the character, read by the backend (the bytes of its 6709)");
    // The teleport numbers (protocol notes 4.16): the client takes a movement state only with a number that is not its own.
    expect(swc::net::takes_travel(0, 1), "the world entry: 1 is not the start number 0 of the client");
    expect(!swc::net::takes_travel(1, 1), "the same number: no travel (the client keeps its prediction)");
    expect(swc::net::takes_travel(1, 2), "a travel: the next number");
    expect(!swc::net::takes_travel(2, 0x12), "the high 4 bits (the migration number) are not a part of the number");
    expect(swc::net::takes_travel(15, 1), "after 15 the number is 1 again (number % 15 + 1)");

    // The facet targets of the research for serial 1: the sum with the carry dropped (not XOR).
    expect(group + swc::net::kPlayerComponent == 0x2B62381C8D27619FULL && group + swc::net::kInteractorComponent == 0xB8AC682EFE6DF614ULL,
           "the facet targets of the player and the interactor (a 64-bit sum)");

    // A keg of a fixture: its reference and the option Trigger of the data (interaction-data.json: action 33, delay 5 s).
    swc::net::InteractionReference keg;
    for (std::size_t index = 0; index < keg.actor.size(); ++index) {
        keg.actor[index] = static_cast<std::uint8_t>(index < 8 ? 0x40 + index : 0);
    }
    keg.group = swc::net::group_of(keg.actor);
    keg.target = 0x69F8F60912B6BC1FULL;
    backend::world::InteractionReference kegBackend;
    kegBackend.actor = keg.actor;
    kegBackend.group = keg.group;
    kegBackend.target = keg.target;
    for (const bool accepted : {true, false}) {
        const Bytes result = rep::interaction_result_body(group, kegBackend, 33, accepted);
        const auto readResult = swc::net::read_interaction_result(result);
        expect(result.size() == 71 && readResult.has_value(), "3743: 71 bytes, the client reads them");
        if (readResult) {
            expect(readResult->call.entity == group && readResult->reference == keg, "3743: the reference of the request");
            expect(readResult->action == 33 && readResult->result == (accepted ? 1 : 2), "3743: the action and the result");
        }
    }

    // The bodies of this client to the readers of the backend.
    const Bytes respawn = swc::net::respawn_request_body(group);
    const auto readRespawn = rep::read_respawn_request(respawn);
    expect(respawn.size() == 18 && readRespawn.has_value(), "1938: 18 bytes (the live client), the backend reads them");
    if (readRespawn) {
        expect(readRespawn->call.entity == group && readRespawn->call.target == group + swc::net::kPlayerComponent, "1938: the entity and the target");
        expect(readRespawn->random && readRespawn->id.empty(), "1938: the row Random Location (random 1, no id)");
    }
    const Bytes clientCancel = swc::net::interaction_cancel_body(group, keg);
    const auto readClientCancel = rep::read_interaction_cancel(clientCancel);
    expect(clientCancel.size() == 48 && readClientCancel.has_value(), "2473: 48 bytes, the backend reads them");
    if (readClientCancel) {
        expect(readClientCancel->entity == group && readClientCancel->target == group + swc::net::kInteractorComponent, "2473: the entity and the target");
        expect(readClientCancel->reference == kegBackend, "2473: the reference of the interaction");
    }

    // The interaction block: byte for byte the block of the made-up client of the backend (no hold time there).
    swc::net::InteractionRequest request;
    request.reference = keg;
    request.action = 33;
    request.baseAction = 33;
    request.optionEntity = 0x4C54561C65E43ABFULL;
    request.committed = true;
    request.optionId = 0x1A6B0F5DU;
    backend::tests::InteractionBlock block;
    block.actor = keg.actor;
    block.group = keg.group;
    block.target = keg.target;
    block.action = 33;
    block.baseAction = 33;
    block.optionEntity = request.optionEntity;
    block.committed = true;
    block.optionId = request.optionId;
    backend::server::hub::Writer theirs;
    backend::tests::write_interaction_block(theirs, block);
    const Bytes ourBlock = swc::net::interaction_block(request);
    expect(ourBlock == theirs.data() && ourBlock.size() == 131, "the interaction block, byte for byte (131 bytes)");

    // A report of three entries: the newest with the timed request (hold 5 s), the next one with no request (bit 8, present 0), the
    // oldest with no bit 8. The reader of the backend: a request only in the newest step.
    request.holdTime = 5.0F;
    const auto entry_of = [](std::uint16_t sequence) {
        swc::net::InputEntry entry;
        entry.sequence = sequence;
        entry.flags = swc::net::kInputFlagsWorld | swc::net::kInputFlagPosition;
        entry.actionWords = 20;
        entry.hasCharacter = true;
        entry.character = {8880.0F, 4250.0F, 138.2F};
        entry.camera = {8880.5F, 4246.0F, 139.9F};
        entry.hasLook = true;
        return entry;
    };
    swc::net::InputEntry newest = entry_of(12);
    newest.interaction = swc::net::interaction_block(request);
    const swc::net::InputEntry middle = entry_of(11);
    const swc::net::InputEntry oldest = entry_of(10);
    const Bytes report = swc::net::input_report_body({newest, middle, oldest}, group + swc::net::kActionListComponent, group);
    const auto input = rep::read_client_input(report);
    expect(input.has_value() && input->steps.size() == 3, "the backend reads the report with the block in each entry");
    if (input && input->steps.size() == 3) {
        const auto& steps = input->steps;
        // The steps of the reader are in the sequence of the input numbers or of the entries: find them by their number.
        const auto step_of = [&steps](std::uint16_t sequence) -> const rep::InputStep* {
            for (const rep::InputStep& step : steps) {
                if (step.sequence == sequence) {
                    return &step;
                }
            }
            return nullptr;
        };
        const rep::InputStep* first = step_of(12);
        const rep::InputStep* second = step_of(11);
        const rep::InputStep* third = step_of(10);
        expect(first && first->interaction.has_value(), "the newest step has the request");
        if (first && first->interaction) {
            const rep::InputInteraction& read = *first->interaction;
            expect(read.actor == keg.actor && read.group == keg.group && read.target == keg.target, "the reference of the block");
            expect(read.action == 33 && read.baseAction == 33 && read.optionEntity == request.optionEntity && read.optionId == request.optionId,
                   "the action, the option entity, and the option id");
            expect(read.committed && read.holdTime == 5.0F, "the committed flag and the hold time");
        }
        expect(second && !second->interaction.has_value(), "an older step with bit 8 and the present byte 0 has no request");
        expect(third && !third->interaction.has_value(), "the oldest step has no request");
        expect(input->hasCharacter && near(input->characterX, 8880.0F, 0.001F), "the position field of the newest entry after the block");
    }
}

/**
 * The interactions of the client (stage P2.10, Interact/SwcInteraction): the ids of an object of the server (the rule of the
 * backend, world/interactions.cpp: K = root XOR G), the focus, and the use of an option (the timed use of the powder keg and its
 * cancels; the hold of the prompt of the loot bag). The templates are the values of interaction-data.json of the backend copy.
 */
void test_interactions() {
    std::printf("the interactions of the client: the ids, the focus, the timed use and its cancels\n");
    namespace interact = swc::interact;
    // The powder keg (template 91): root 000178a376a472d6; the Interact bound of slot 10 (component 1da3a6bfd2b6bb1f, entity
    // 000178b976a472d6, rangeSq 25); the option Trigger (id 443223901, action 33, committed, Delay Time 5 s, option entity
    // 000061ec53404969).
    interact::Template keg;
    keg.name = "powderKeg";
    keg.asset = {0x30, 0xb6, 0x34, 0x73, 0x6f, 0xd6, 0x53, 0xee, 0xa8, 0x9f, 0x3a, 0x9c, 0x00, 0x45, 0xe0, 0xf7};
    keg.sub = 2;
    keg.root = 0x000178a376a472d6ULL;
    interact::Bound bound;
    bound.slot = 10;
    bound.component = 0x1da3a6bfd2b6bb1fULL;
    bound.entity = 0x000178b976a472d6ULL;
    bound.rangeSq = 25.0F;
    interact::Option trigger;
    trigger.name = "Trigger";
    trigger.id = 443223901U;
    trigger.action = 33;
    trigger.committed = true;
    trigger.delay = 5.0F;
    trigger.entity = 0x000061ec53404969ULL;
    bound.options.push_back(trigger);
    keg.bounds.push_back(bound);
    // A keg of the allocator with G = 4C554F5340000100 (the id form of the backend: serial * 2, "SOUL").
    const swc::net::ActorId actor{0x00, 0x01, 0x00, 0x40, 0x53, 0x4F, 0x55, 0x4C, 0x20, 0x00, 0x00, 0x00, 0x53, 0x4F, 0x55, 0x4C};
    expect(swc::net::group_of(actor) == 0x4C554F5340000100ULL, "the entity group of the keg");
    expect(interact::instance_of(keg.root, 0x4C554F5340000100ULL, bound.entity) == 0x4C554F4940000100ULL, "the instance of the Interact entity");
    const auto request = interact::request_of(keg, bound, trigger, actor);
    expect(request.reference.target == 0x69F8F60912B6BC1FULL, "the facet target of the bound (the instance + the component)");
    expect(request.optionEntity == 0x4C54561C65E43ABFULL, "the option entity (XOR K)");
    expect(request.action == 33 && request.baseAction == 33 && request.committed && request.optionId == 443223901U && request.holdTime == 5.0F,
           "the action, the base action, the option id, and the hold time of the block");

    // The focus: the keg 3 m from the character (in the range of 5 m), a second keg 6 m away (out of the range).
    std::vector<interact::Template> templates{keg};
    interact::Object near_keg{7, actor, keg.asset, 2, {100.0F, 203.0F, 50.0F}};
    swc::net::ActorId farActor = actor;
    farActor[0] = 0x02;
    interact::Object far_keg{8, farActor, keg.asset, 2, {106.0F, 200.0F, 50.0F}};
    const swc::net::WorldPosition feet{100.0F, 200.0F, 50.0F};
    const auto focus = interact::focus(templates, {far_keg, near_keg}, feet);
    expect(focus && focus->interest == 7 && near(focus->distanceSq, 9.0F, 0.01F), "the focus is the keg in the range");
    expect(!interact::focus(templates, {far_keg}, feet), "no focus out of the range");
    const swc::net::WorldPosition edge{100.0F, 198.0F, 50.0F};
    expect(interact::focus(templates, {near_keg}, edge).has_value(), "the range: an equal distance passes (5 m)");
    if (!focus) {
        return;
    }

    // A release before the end of the Delay Time: the cancel 2473, no execution.
    interact::Use use;
    use.press(10.0, *focus);
    auto effects = use.take();
    expect(use.phase() == interact::Use::Phase::requested && effects.requestChanged && effects.request && effects.request->optionId == trigger.id,
           "the press: the request at once (no hold time)");
    use.tick(12.0);
    expect(near(use.progress(12.0), 0.4F, 0.001F), "the progress of the Delay Time (2 of 5 s)");
    use.release(12.0);
    effects = use.take();
    expect(use.phase() == interact::Use::Phase::idle && effects.cancel && use.executions() == 0, "a release at 2 s: the cancel, no execution");

    // The reply 1 (20 ms after the press), then the end of the Delay Time: the execution at 5 s; the request ends with no message
    // 0.03 s after it and not before the Delay Time after the reply (the timer of the server starts at its accept).
    use.press(20.0, *focus);
    (void)use.take();
    use.reply(1, 20.02);
    use.tick(24.99);
    expect(use.phase() == interact::Use::Phase::requested, "before 5 s the use goes on");
    use.tick(25.0);
    effects = use.take();
    expect(use.executions() == 1 && use.phase() == interact::Use::Phase::executed && !effects.requestChanged, "at 5 s the execution; the request stays");
    use.tick(25.03);
    expect(use.phase() == interact::Use::Phase::executed, "0.03 s after the execution: the timer of the server is not due yet (the reply came at 0.02 s)");
    use.tick(25.05);
    effects = use.take();
    expect(use.phase() == interact::Use::Phase::idle && effects.requestChanged && !effects.request && !effects.cancel,
           "after the Delay Time from the reply: the request ends with no message");
    use.release(25.5);
    expect(!use.take().cancel, "the release after the end sends nothing");
    // A release just after the execution with the reply 1: no cancel, the request ends at its time.
    use.press(26.0, *focus);
    (void)use.take();
    use.reply(1, 26.01);
    use.tick(31.0);
    use.release(31.0);
    effects = use.take();
    expect(!effects.cancel && use.phase() == interact::Use::Phase::executed, "a release at the execution with the reply 1: no cancel");
    use.tick(31.05);
    effects = use.take();
    expect(use.phase() == interact::Use::Phase::idle && effects.requestChanged && !effects.request, "then the request ends at its time");

    // No reply: after the execution the client keeps its request until the release, then the cancel.
    use.press(30.0, *focus);
    (void)use.take();
    use.tick(35.1);
    effects = use.take();
    expect(use.phase() == interact::Use::Phase::executed && !effects.cancel && !effects.requestChanged, "the execution with no reply: the request stays");
    use.release(36.0);
    effects = use.take();
    expect(effects.cancel && use.phase() == interact::Use::Phase::idle, "then the release: the cancel");

    // 3746 during the hold of the key: the use ends (the session client sends 2473 and clears the block itself).
    use.press(40.0, *focus);
    (void)use.take();
    use.tick(41.0);
    use.server_cancel();
    effects = use.take();
    expect(use.phase() == interact::Use::Phase::idle && !effects.cancel && use.serverEnds() == 1, "3746: the use ends, no second cancel");
    use.tick(45.5);
    expect(use.executions() == 3, "no execution after the end by the server");
    use.release(46.0);
    expect(!use.take().cancel, "the release after the end by the server sends nothing");

    // The reply 2: the request ends (the session client cleared the block).
    use.press(50.0, *focus);
    (void)use.take();
    use.reply(2, 50.02);
    use.tick(56.0);
    expect(use.phase() == interact::Use::Phase::idle && use.executions() == 3, "the reply 2: no execution");

    // The death in a timed use: the cancel.
    use.press(60.0, *focus);
    (void)use.take();
    use.abort();
    expect(use.take().cancel && use.phase() == interact::Use::Phase::idle, "the death in a use: the cancel");

    // The loot bag: TakeAllOption (action 28, no Delay Time, the hold of the prompt 0.5 s).
    interact::Template bag = keg;
    bag.bounds[0].options[0].name = "TakeAllOption";
    bag.bounds[0].options[0].action = 28;
    bag.bounds[0].options[0].delay = 0.0F;
    bag.bounds[0].options[0].hold = 0.5F;
    // The candidate points into the templates: they stay alive while it is used.
    const std::vector<interact::Template> bagTemplates{bag};
    const auto bagFocus = interact::focus(bagTemplates, {near_keg}, feet);
    expect(bagFocus.has_value(), "the focus of the bag");
    if (bagFocus) {
        interact::Use take;
        take.press(70.0, *bagFocus);
        take.tick(70.3);
        take.release(70.3);
        expect(take.requests() == 0 && !take.take().requestChanged, "a release before the hold time: no request");
        take.press(71.0, *bagFocus);
        take.tick(71.5);
        effects = take.take();
        expect(take.requests() == 1 && effects.request && take.phase() == interact::Use::Phase::executed, "the hold of 0.5 s: the request and the execution");
        take.reply(1, 71.52);
        take.tick(71.56);
        effects = take.take();
        expect(take.phase() == interact::Use::Phase::idle && effects.requestChanged && !effects.request, "the reply 1: the request ends");
    }
    // The Take All of a loot bag keeps its request after the execution and the reply 1 (the client waits for the server); the
    // release does not end it; the Looted claim of the bag ends it with 2473 (protocol notes 4.42, live check L6).
    if (bagFocus) {
        interact::Use take;
        take.press(80.0, *bagFocus, true);
        take.tick(80.5);
        (void)take.take();
        take.reply(1, 80.52);
        take.tick(81.0);
        take.release(81.0);
        effects = take.take();
        expect(take.phase() == interact::Use::Phase::executed && !effects.cancel && !effects.requestChanged,
               "the Take All of a bag: the request stays after the reply 1 and the release");
        take.finish();
        effects = take.take();
        expect(take.phase() == interact::Use::Phase::idle && effects.cancel, "the Looted claim: the client ends the use (2473)");
    }
    // The range at the request (docs/INTERACTIONS.md "The range"): the server measures from the position field of the report that
    // has the request. A hold whose character goes out of the range ends with no request; in the range the hold goes on.
    if (bagFocus) {
        interact::Use take;
        take.press(90.0, *bagFocus);
        expect(near(take.rangeSq(), 25.0F, 0.0F) && !take.moved(edge), "a hold at the edge of the range (5 m) goes on");
        expect(take.moved(swc::net::WorldPosition{100.0F, 197.9F, 50.0F}) && take.phase() == interact::Use::Phase::idle && take.rangeEnds() == 1,
               "a hold 5.1 m from the bound ends");
        take.tick(90.5);
        const auto ended = take.take();
        expect(take.requests() == 0 && !ended.requestChanged && !ended.cancel, "the end of the hold out of the range: no request, no cancel");
    }
    // A server child turns the bounds of its slice with its rotation (backend gamedata::transform_point): the bound of a camp 0.835 m
    // to the south of its origin is 0.835 m to the north of it at a yaw of 180 degrees.
    interact::Template camp = bag;
    camp.bounds[0].position[1] = -0.835F;
    const std::vector<interact::Template> campTemplates{camp};
    interact::Object turned = near_keg;
    turned.rotation = swc::net::Quaternion{0.0F, 0.0F, 1.0F, 0.0F};
    const auto campFocus = interact::focus(campTemplates, {turned}, feet);
    expect(campFocus && near(campFocus->place.y, 203.835F, 1.0e-4F) && near(campFocus->distanceSq, 3.835F * 3.835F, 1.0e-3F),
           "the bound of a camp turned by 180 degrees: north of its origin");
    const float local[3]{0.0F, -0.835F, 0.0F};
    const float half = 0.5F * 1.5707964F;
    const swc::net::WorldPosition east = interact::transform_point({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, std::sin(half), std::cos(half)}, local);
    expect(near(east.x, 0.835F, 1.0e-5F) && near(east.y, 0.0F, 1.0e-5F), "a yaw of 90 degrees turns the south to the east");

    // The position field of the next input entry (RepClient::next_reported): the character with no last field, after a travel, and
    // at rest; a fall of 1.13 m in one step (the limit is 2 m); a long frame of 1 m (the limit is 0.333 m in one step).
    const swc::net::WorldPosition fall{100.0F, 200.0F, 48.87F};
    expect(near(swc::net::RepClient::next_reported(std::nullopt, fall).z, 48.87F, 0.0F), "no last field: the character");
    expect(near(swc::net::RepClient::next_reported(feet, swc::net::WorldPosition{100.0F, 230.0F, 50.0F}).y, 230.0F, 0.0F), "a travel of 30 m: the character");
    expect(near(swc::net::RepClient::next_reported(feet, feet).y, 200.0F, 0.0F), "at rest: the same field");
    expect(near(swc::net::RepClient::next_reported(feet, fall).z, 48.87F, 0.0F), "a fall of 1.13 m: the landing in one step");
    expect(near(swc::net::RepClient::next_reported(feet, swc::net::WorldPosition{101.0F, 200.0F, 50.0F}).x, 100.0F + 10.0F / 30.0F, 1.0e-5F),
           "a move of 1 m in one frame: 0.333 m in one step");
}

/**
 * The loot (stage P2.10): the take messages 2939 and 2942 to the reader of the backend (server/rep/item_requests); the readers of
 * the client for the LootTracker map (slot 33), the GlobalStorage map (slot 77), and the Container (slots 61, 76: the full vector
 * and a journal), with fields in the layout of the protocol notes 4.10, 4.41, 4.42 (the made-up values of the backend fixtures).
 */
void test_loot() {
    std::printf("the loot: the take messages against the reader of the backend, the readers of the loot states\n");
    namespace rep = backend::server::rep;
    const swc::net::ActorId actor{0x02, 0x00, 0x00, 0x00, 0x53, 0x4F, 0x55, 0x4C, 0x10, 0x00, 0x00, 0x00, 0x53, 0x4F, 0x55, 0x4C};
    const std::uint64_t group = swc::net::group_of(actor);
    expect(group == 0x4C554F5300000002ULL, "G of serial 1");
    // The values of the backend fixture (tests/support/loot_bag_fixture.h): the GlobalStorage target and the PlayerTrade target.
    expect(swc::net::global_storage_target(group) == 0xDE23CE47F759DECEULL, "the facet target of the own GlobalStorage Container");
    expect(group + swc::net::kPlayerTradeComponent == 0xAAC3F4219484F844ULL, "the facet target of the PlayerTrade facet");
    const Bytes all = swc::net::take_all_items_body(actor);
    const auto readAll = rep::read_bag_request(all, false);
    expect(all.size() == 48 && readAll.has_value(), "2939: 48 bytes, the backend reads them");
    if (readAll) {
        expect(readAll->call.entity == group && readAll->call.target == group + swc::net::kPlayerTradeComponent, "2939: the entity and the target");
        expect(readAll->container.actor == actor && readAll->container.group == group
                   && readAll->container.target == swc::net::global_storage_target(group),
               "2939: the reference names the own GlobalStorage Container");
    }
    std::array<std::uint8_t, 16> instance{};
    for (std::size_t index = 0; index < instance.size(); ++index) {
        instance[index] = static_cast<std::uint8_t>(0xA0 + index);
    }
    const Bytes one = swc::net::take_item_body(actor, instance, 9);
    const auto readOne = rep::read_bag_request(one, true);
    expect(one.size() == 68 && readOne.has_value(), "2942: 68 bytes, the backend reads them");
    if (readOne) {
        expect(readOne->container.target == swc::net::global_storage_target(group), "2942: the reference");
        expect(std::equal(instance.begin(), instance.end(), readOne->instance.begin()) && readOne->quantity == 9, "2942: the instance and the quantity");
    }
    expect(!rep::read_bag_request(one, false).has_value() && !rep::read_bag_request(all, true).has_value(), "the backend refuses a body of the other form");

    // A LootTracker map in its full form: changes 0, no version, two claims of 58 bytes (a Rolled bag and a Looted bag).
    swc::net::Writer tracker;
    tracker.size(0);
    tracker.u8(0);
    tracker.size(2);
    for (const std::uint32_t state : {swc::net::kLootRolled, swc::net::kLootLooted}) {
        swc::net::ActorId bag{};
        bag[0] = static_cast<std::uint8_t>(0x30 + state);
        tracker.raw(bag);
        tracker.u32(state);
        tracker.u32(1);            // AILoot
        tracker.u64(1000000ULL);   // resetTime
        tracker.u64(state == swc::net::kLootLooted ? 2000000ULL : 0ULL);
        tracker.u8(0);
        tracker.f32(8880.5F);
        tracker.f32(4250.25F);
        tracker.f32(138.0F);
        tracker.u32(0);
        tracker.u8(0);
    }
    const Bytes trackerBytes = tracker.data();
    expect(trackerBytes.size() == 3 + 2 * 58, "the LootTracker map: 2 entries of 58 bytes");
    const auto claims = swc::net::read_loot_tracker(trackerBytes);
    expect(claims && claims->size() == 2, "the LootTracker reader: two claims");
    if (claims && claims->size() == 2) {
        expect((*claims)[0].actor[0] == 0x31 && (*claims)[0].state == swc::net::kLootRolled && (*claims)[0].kind == 1, "a Rolled bag of a kill");
        expect((*claims)[1].state == swc::net::kLootLooted && (*claims)[1].lootedTime == 2000000ULL, "a Looted bag");
        expect(near((*claims)[0].place.x, 8880.5F, 0.001F) && near((*claims)[0].place.z, 138.0F, 0.001F), "the place of the claim");
    }
    swc::net::Writer empty;
    empty.size(0);
    empty.u8(0);
    empty.size(0);
    const auto none = swc::net::read_loot_tracker(empty.data());
    expect(none && none->empty(), "the map with no entry (the server ends the claims)");

    // An item of the wire in the layout of the reader of the client (protocol notes 4.10; the vendored item_decoder.h of the backend
    // tests): packed = maxDurability + 8001 x (index + 2^20 x (slot + 502 x (action + 64 x quantity))), the sizes durability, chrono,
    // gear score, five perks, 4 dye bytes, the instance, the flags, the appearance, 4 skin dye bytes.
    const auto write_item = [](swc::net::Writer& w, std::uint32_t index, std::uint32_t quantity, std::uint8_t mark) {
        const std::uint64_t slot = 501;   // no slot
        const std::uint64_t packed = 750 + 8001ULL * (index + (1ULL << 20) * (slot + 502ULL * (0x11 + 64ULL * quantity)));
        w.u64(packed);
        w.size(750);
        w.size(0);
        w.size(75);
        for (int perk = 0; perk < 5; ++perk) {
            w.size(0);
        }
        w.zeros(4);
        for (int index2 = 0; index2 < 16; ++index2) {
            w.u8(static_cast<std::uint8_t>(mark + index2));
        }
        w.u8(0);
        w.u32(0);
        w.zeros(4);
    };
    swc::net::Writer storage;
    storage.size(0);
    storage.u8(0);
    storage.size(2);
    swc::net::ActorId bagActor{};
    bagActor[0] = 0x31;
    storage.raw(bagActor);
    storage.size(1);
    write_item(storage, 1234, 9, 0x50);
    swc::net::ActorId emptyBag{};
    emptyBag[0] = 0x32;
    storage.raw(emptyBag);
    storage.size(0);
    const auto keys = swc::net::read_global_storage(storage.data());
    expect(keys && keys->size() == 2, "the GlobalStorage map: two keys");
    if (keys && keys->size() == 2) {
        const auto& item = (*keys)[0].items;
        expect((*keys)[0].actor == bagActor && item.size() == 1 && item[0].index == 1234 && item[0].quantity == 9 && item[0].gearScore == 75,
               "the key of the bag: one item (its index, quantity, gear score)");
        expect(item.size() == 1 && item[0].instance[0] == 0x50 && item[0].durability == 750, "the instance and the durability of the item");
        expect((*keys)[1].actor == emptyBag && (*keys)[1].items.empty(), "a key with no item");
    }

    // The Container: the full vector of two slots (an empty one and an item), then a journal of two entries that sets slot 0 and
    // adds slot 2 (the length of the vector), then a journal that empties slot 1 (the backend sends the empty element).
    std::vector<backend::tests::WireItem> elements;
    swc::net::Writer full;
    full.size(0);
    full.u8(0);
    full.size(2);
    write_item(full, 0, 0, 0x00);
    write_item(full, 77, 1, 0x60);
    expect(swc::net::apply_container(full.data(), elements) && elements.size() == 2 && elements[0].empty() && elements[1].index == 77,
           "the full vector: an empty slot and an item");
    swc::net::Writer journal;
    journal.size(2);
    journal.u8(0x03);              // the mask: both entries set an element
    journal.size64(0);
    journal.u8(0);
    write_item(journal, 1234, 9, 0x50);
    journal.size64(2);
    journal.u8(0);
    write_item(journal, 88, 3, 0x70);
    expect(swc::net::apply_container(journal.data(), elements) && elements.size() == 3, "a journal: the vector has 3 slots");
    expect(elements.size() == 3 && elements[0].index == 1234 && elements[0].quantity == 9 && elements[2].index == 88, "the set slot and the new slot");
    swc::net::Writer emptied;
    emptied.size(1);
    emptied.u8(0x01);
    emptied.size64(1);
    emptied.u8(0);
    write_item(emptied, 0, 0, 0x00);
    expect(swc::net::apply_container(emptied.data(), elements) && elements.size() == 3 && elements[1].empty(), "a slot that loses its item: the empty element");
    swc::net::Writer bad;
    bad.size(1);
    expect(!swc::net::apply_container(bad.data(), elements), "a journal that ends early has no form");
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

/**
 * Stage P4.8 (docs/PERSONS.md "The Talk"): the conversation of the client with the use of OpenConversation, in the order of the
 * interactor (Interact/SwcInteractor.cpp): the request at the end of the hold opens it; the ends send 6705 first, then 2473 only for
 * the end by the player; the input stays blocked until the next answer 6709 (the count of the session client). The camera point of
 * the commander of Oxboro against the live camera of the retail client (research p48-talk/camera-check.txt).
 */
void test_conversation() {
    std::printf("the conversation (stage P4.8)\n");
    using swc::interact::Conversation;
    using swc::interact::Use;
    // OpenConversation of the interaction data (option 17): action 63, committed, no Delay Time, hold 0.5 s.
    swc::interact::Option talkOption;
    talkOption.name = "OpenConversation";
    talkOption.action = 63;
    talkOption.committed = true;
    talkOption.hold = 0.5F;
    swc::interact::Candidate candidate;
    candidate.option = &talkOption;
    candidate.rangeSq = 25.0F;
    // One run of the interactor order: press, the hold, then the open at the execution.
    const auto start = [&](Use& use, Conversation& talk, std::uint32_t lockEnds) {
        talk.update(lockEnds);
        use.press(0.0, candidate, true);
        use.tick(0.49);
        const bool noEarly = use.phase() == Use::Phase::holding;
        const std::uint32_t executions = use.executions();
        use.tick(0.5);
        if (use.executions() != executions) {
            talk.open();
        }
        const Use::Effects effects = use.take();
        return noEarly && effects.requestChanged && effects.request.has_value() && talk.scene() && talk.locked();
    };
    {
        Use use;
        Conversation talk;
        expect(start(use, talk, 3), "the request at the end of the hold (0.5 s) opens the conversation: the scene, the lock");
        use.release(0.6);
        use.reply(1, 0.6);
        use.tick(10.0);
        expect(use.phase() == Use::Phase::executed && !use.take().requestChanged && talk.scene(),
               "the release and the reply 1 keep the request and the conversation (the block stays in each input entry)");
        // E in the conversation: player_close, then the finish of the use (the order of the effects: 6705, then 2473).
        talk.update(3);
        talk.player_close();
        use.finish();
        const Conversation::Effects ends = talk.take();
        const Use::Effects useEnds = use.take();
        expect(ends.endCutscene && ends.cancelInteraction && useEnds.cancel && !useEnds.request, "the end by the player: 6705 and 2473");
        expect(!talk.scene() && talk.locked(), "after the 6705: no scene (the camera is free), the input stays blocked");
        talk.update(3);
        expect(talk.locked() && !talk.take().endCutscene, "no answer: the lock stays, and no second 6705");
        talk.update(4);
        expect(!talk.locked() && talk.phase() == Conversation::Phase::closed, "the answer 6709 (the count 3 to 4) frees the input");
    }
    {
        Use use;
        Conversation talk;
        expect(start(use, talk, 7), "a second conversation opens");
        use.reply(2, 0.6);
        talk.refused();
        const Conversation::Effects ends = talk.take();
        const Use::Effects useEnds = use.take();
        expect(use.phase() == Use::Phase::idle && ends.endCutscene && !ends.cancelInteraction && !useEnds.cancel,
               "the refusal (3743 result 2, the person of a client spawner): 6705, no 2473");
        talk.update(8);
        expect(!talk.locked(), "the answer 6709 frees the input after the refusal");
    }
    {
        Use use;
        Conversation talk;
        expect(start(use, talk, 0), "a third conversation opens");
        use.server_cancel();
        talk.server_ended();
        const Conversation::Effects ends = talk.take();
        expect(use.phase() == Use::Phase::idle && ends.endCutscene && !ends.cancelInteraction && !use.take().cancel,
               "the end by the server (3746; the session client sent 2473): 6705");
        talk.player_close();
        expect(!talk.take().endCutscene, "a close after the end sends nothing");
        talk.update(1);
        expect(!talk.locked(), "the answer 6709 frees the input");
    }
    // The commander of Oxboro (0407a_Commander, yaw 205 degrees): the preset NSL_FAC_MED_SNGL_LA, origin (0.5, 2.3, 0.5) in her frame;
    // the live camera of the retail client at 8836.71 4243.79 137.04 (the height of the live camera has the ground of the run).
    const swc::interact::ScenePoint camera =
        swc::interact::scene_point({8836.1943359375, 4246.087890625, 136.48809814453125}, 205.0, {0.5, 2.3, 0.5});
    expect(std::hypot(camera.x - 8836.71, camera.y - 4243.79) < 0.01 && std::abs(camera.z - 137.04) < 0.06,
           "the camera of the commander: 0.00 m from the live retail camera in the horizontal plane");
    const swc::interact::ScenePoint side = swc::interact::scene_point({0.0, 0.0, 0.0}, 90.0, {1.0, 0.0, 0.0});
    const swc::interact::ScenePoint front = swc::interact::scene_point({0.0, 0.0, 0.0}, 90.0, {0.0, 1.0, 0.0});
    expect(std::abs(side.y - 1.0) < 1e-9 && std::abs(front.x + 1.0) < 1e-9, "the frame at yaw 90: the side axis is +y, the forward axis is -x");
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
    test_life_messages();
    test_interactions();
    test_conversation();
    test_loot();
    test_carrier();
    std::printf("Result: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
