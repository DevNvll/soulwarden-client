// Tests of the combat codecs of the creature view against the backend. The melee contact of a creature
// (Creatures/SwcMeleeContact: the facet target and the fields of the item; the writer of the input codec,
// swc::net::write_melee_contact): the made-up client of the backend tests writes the same item (tests/support/client_frames.h,
// InputContactItem), and the reader of the backend (server/rep/client_input.cpp) reads the item of the client in list 3 of an
// input entry. The damage number (Creatures/SwcDamageMessage): the client reads the bytes of the backend encoder
// (server/rep/combat_messages.cpp). tools/test-creatures.sh builds it with the system compiler; the Soulwarden sources are read
// only.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "Creatures/SwcDamageMessage.h"
#include "Creatures/SwcMeleeContact.h"
#include "server/rep/client_input.h"
#include "server/rep/combat_messages.h"
#include "support/client_frames.h"

namespace {

namespace rep = soulwarden::backend::server::rep;
namespace frames = soulwarden::backend::tests;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

/** A creature: an actor id of the backend form (world/ids.h) and a hit volume component of its slice. */
struct Creature {
    swc::net::ActorId actor{0x41, 0x97, 0x2C, 0x0E, 0x11, 0x00, 0x00, 0x80, 0x52, 0x63, 0x74, 0x85, 0x96, 0xA7, 0xB8, 0xC9};
    std::uint64_t root{0x6F3A2B1C0D4E5F60ULL};
    std::uint64_t entity{0x1234ABCD5678EF01ULL};
    std::uint64_t component{0x9B8A79685746352FULL};
};

swc::net::MeleeContactItem contact_of(const Creature& creature) {
    return swc::creatures::melee_contact_item(
        creature.actor, swc::creatures::facet_target(swc::net::group_of(creature.actor), creature.entity ^ creature.root, creature.component),
        {8815.25F, 4319.5F, 141.0F}, {0.6F, -0.8F, 0.0F}, {-0.8F, -0.6F, 0.0F});
}

swc::net::Bytes bytes_of(const swc::net::MeleeContactItem& contact) {
    swc::net::Writer out;
    swc::net::write_melee_contact(out, contact);
    return out.data();
}

std::array<float, 3> array_of(const swc::net::WorldPosition& value) {
    return {value.x, value.y, value.z};
}

void test_facet_target() {
    const Creature creature;
    const std::uint64_t group = swc::net::group_of(creature.actor);
    check(group == 0x800000110E2C9741ULL, "G: the first 8 bytes of the actor id, little-endian");
    // CreatureSlice::target of the backend (gamedata/creature_data.h): (component.entity ^ root ^ group) + component.component.
    const std::uint64_t backend = (creature.entity ^ creature.root ^ group) + creature.component;
    check(swc::creatures::facet_target(group, creature.entity ^ creature.root, creature.component) == backend,
          "the facet target of the client is the target of the backend slice");
    // The sum wraps at 64 bits, as the backend does.
    check(swc::creatures::facet_target(0, 0xFFFFFFFFFFFFFFFFULL, 2) == 1, "the facet target wraps at 64 bits");
}

void test_bytes_of_the_backend_writer() {
    const Creature creature;
    const swc::net::MeleeContactItem contact = contact_of(creature);
    const swc::net::Bytes item = bytes_of(contact);
    check(item.size() == swc::net::kMeleeContactSize, "the item has 96 bytes");
    check(item[0] == 0xFF && item[1] == 0xFF && item[2] == 0xFF && item[3] == 0xFF, "the provider 0xFFFFFFFF (none)");

    frames::InputContactItem made;
    made.groupActor = creature.actor;
    made.group = swc::net::group_of(creature.actor);
    made.actor = creature.actor;
    made.entity = made.group;
    made.facet = contact.facet;
    made.point = array_of(contact.point);
    made.normal = array_of(contact.normal);
    made.direction = array_of(contact.direction);
    frames::InputEntry entry = frames::report_entry(41, 8814.0F, 4318.0F, 140.0F);
    entry.contacts = {made};
    const swc::net::Bytes body = frames::input_entry_body(entry);
    // The item of the client is in the body of the backend writer one time.
    std::size_t found = 0;
    for (auto at = body.begin(); (at = std::search(at, body.end(), item.begin(), item.end())) != body.end(); ++at) {
        ++found;
    }
    check(found == 1, "the backend test client writes the same 96 bytes (" + std::to_string(found) + " times in its body)");
    // The count of list 3 is just before the item: 1.
    const auto at = std::search(body.begin(), body.end(), item.begin(), item.end());
    check(at != body.begin() && at != body.end() && *(at - 1) == 1, "the count of list 3 before the item: 1");
}

void test_backend_reader() {
    const Creature creature;
    const swc::net::MeleeContactItem contact = contact_of(creature);
    // The entry of the backend writer with no contact, then the item of the client in place of the empty list 3.
    const frames::InputEntry entry = frames::report_entry(42, 8814.0F, 4318.0F, 140.0F);
    swc::net::Bytes body = frames::input_entry_body(entry);
    frames::InputEntry withOne = entry;
    withOne.contacts = {frames::InputContactItem{}};
    const swc::net::Bytes reference = frames::input_entry_body(withOne);
    // The bodies are equal up to the count of list 3 (0 or 1): there the client puts its count and its item.
    std::size_t count = 0;
    while (count < body.size() && body[count] == reference[count]) {
        ++count;
    }
    check(count < body.size() && body[count] == 0 && reference[count] == 1, "the count of list 3 in the body of the backend writer");
    body[count] = 1;
    const swc::net::Bytes item = bytes_of(contact);
    body.insert(body.begin() + static_cast<std::ptrdiff_t>(count) + 1, item.begin(), item.end());

    const std::optional<rep::ClientInput> input = rep::read_client_input(body);
    check(input.has_value() && input->tail == rep::InputTail::read, "the backend reads the entry with the item of the client to its end");
    check(input.has_value() && input->hasCharacter, "the entry has the position of the character");
    const rep::InputContact* read = input && !input->steps.empty() && input->steps.front().contacts.size() == 1
                                        ? &input->steps.front().contacts.front()
                                        : nullptr;
    check(read != nullptr, "the backend reads one contact");
    if (read == nullptr) {
        return;
    }
    const std::uint64_t group = swc::net::group_of(creature.actor);
    check(read->provider == 0xFFFFFFFFU, "the provider");
    check(read->groupActor == creature.actor && read->group == group, "the group: the actor id and G of the creature");
    check(read->actor == creature.actor && read->entity == group, "the receiver: the actor id and G of the creature");
    check(read->facet == contact.facet, "the facet target of the hit volume");
    check(read->point == array_of(contact.point) && read->normal == array_of(contact.normal) && read->direction == array_of(contact.direction),
          "the point, the normal, and the direction");
}

soulwarden::backend::world::DamageDealt damage_of(const Creature& creature) {
    soulwarden::backend::world::DamageDealt damage;
    std::copy(creature.actor.begin(), creature.actor.end(), damage.targetActor.bytes.begin());
    damage.target = swc::net::group_of(creature.actor);
    damage.type = 4;
    damage.amount = 64.0F;
    return damage;
}

void test_damage_message() {
    const Creature creature;
    const std::uint64_t attacker = 0x0000000200000001ULL;
    const swc::net::Bytes body = rep::damage_dealt_body(attacker, damage_of(creature));
    const std::optional<swc::creatures::DamageMessage> read = swc::creatures::read_damage_message(body);
    check(read.has_value(), "the client reads the damage number of the backend encoder");
    if (!read) {
        return;
    }
    check(read->receiverTarget == attacker + rep::kPlayerDamageReceiver && read->receiverEntity == attacker,
          "the damage receiver facet of the attacker");
    check(read->actor == creature.actor && read->group == swc::net::group_of(creature.actor), "the entity that took the damage");
    check(read->flags == 0 && read->entries.size() == 1, "no flag, one entry");
    check(read->entries.size() == 1 && read->entries[0].type == 4 && read->entries[0].amount == 64.0F && read->entries[0].part == 1.0F,
          "the entry: slash, 64, part 1");

    // The kill with a converted part (a gem: a second entry) and the other flags of the hit.
    soulwarden::backend::world::DamageDealt kill = damage_of(creature);
    kill.killed = true;
    kill.secondAmount = 12.5F;
    kill.secondType = 9;
    kill.flags = swc::creatures::DamageMessage::kCanCrit | swc::creatures::DamageMessage::kCrit | swc::creatures::DamageMessage::kHeadshot;
    const std::optional<swc::creatures::DamageMessage> killed = swc::creatures::read_damage_message(rep::damage_dealt_body(attacker, kill));
    check(killed.has_value() && (killed->flags & swc::creatures::DamageMessage::kCausedDeath) != 0, "the kill: bit 3");
    check(killed.has_value() && (killed->flags & swc::creatures::DamageMessage::kHeadshot) != 0 && (killed->flags & swc::creatures::DamageMessage::kCrit) != 0,
          "the flags of the second byte (headshot) and of the first byte (critical hit)");
    check(killed.has_value() && killed->entries.size() == 2 && killed->entries[1].type == 9 && killed->total() == 76.5F,
          "the second entry; the total 76.5");

    // A body that is too short or too long is no message.
    swc::net::Bytes cut = body;
    cut.pop_back();
    check(!swc::creatures::read_damage_message(cut).has_value(), "a short body is no message");
    swc::net::Bytes longer = body;
    longer.push_back(0);
    check(!swc::creatures::read_damage_message(longer).has_value(), "a body with more bytes is no message");
}

} // namespace

int main() {
    test_facet_target();
    test_bytes_of_the_backend_writer();
    test_backend_reader();
    test_damage_message();
    std::printf("combat: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
