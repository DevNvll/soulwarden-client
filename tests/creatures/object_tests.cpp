// Tests of the objects of the level data (stage P4.1 part 2; Objects/SwcObjectRules, Creatures/SwcEntityTable, the read path of the
// Achievement state): the ids of an object by the backend rule against the values of the backend data (interaction-data.json and
// gathering-data.json of the backend copy: the actor, the facet target, and the option entity of a lore page, a chest, a capital
// node, and a natural node; backend test cases IT10 and GAT2), the record of a gathering node with the states that the encoders of
// the backend make (metadata, Interact, GatherableController: server/rep/states), the Achievement state of the owner
// (ownership_states.cpp), the Gathering state of the owner (chest_states.cpp) and the end of a gather use (Interact/SwcGatherUse),
// and the file of the natural nodes (converter/swc_convert/objects.py). tools/test-creatures.sh builds it with the system compiler;
// the Soulwarden sources are read only.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Creatures/SwcEntityTable.h"
#include "Interact/SwcGatherUse.h"
#include "Net/Codec/SwcRepMessages.h"
#include "Objects/SwcObjectRules.h"
#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "support/state_decoder.h"
#include "world/entity.h"
#include "world/world_clock.h"

namespace {

namespace backend = soulwarden::backend;
namespace decoder = soulwarden::backend::tests;
namespace rep = soulwarden::backend::server::rep;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

class TestClock final : public backend::world::WorldClock {
public:
    [[nodiscard]] time_point now() const override { return m_now; }

protected:
    [[nodiscard]] std::int64_t wall_offset_ns() const override { return 0; }

private:
    time_point m_now{std::chrono::seconds(1000)};
};

decoder::Fragment fragment_of(std::uint32_t slot, std::uint32_t type, const backend::server::hub::Bytes& state, const char* name) {
    decoder::Fragment fragment;
    fragment.slot = slot;
    fragment.type = type;
    std::size_t position = 0;
    const bool read = decoder::detail::read_state(state, position, fragment);
    check(read && position == state.size(), std::string(name) + ": the state reader reads the state of the encoder to its end");
    return fragment;
}

std::string hex_actor(const swc::net::ActorId& actor) {
    return swc::net::hex_of(actor);
}

swc::net::ActorId placement(const char* uuid) {
    const auto bytes = swc::objects::parse_uuid(uuid);
    check(bytes.has_value(), std::string("the UUID ") + uuid + " reads");
    return bytes ? swc::objects::placement_actor(*bytes) : swc::net::ActorId{};
}

// ---------------------------------------------------------------------------------------------------------------- the ids

void test_ids() {
    // A lore page of r_+04_+02 (interaction-data.json): actor c3099bdc2acb20b5, slot 2, target 8cf752924a848ca2, the option
    // entity of ReadLore b520cb2edc9b09c3.
    const swc::net::ActorId lore = placement("bdf81e34-8c29-69ca-08d8-d51e50b2600c");
    check(hex_actor(lore) == "c3099bdc2acb20b50000000000000000", "the actor of a lore page from its UUID");
    check(swc::net::group_of(lore) == 0xB520CB2ADC9B09C3ULL, "G of the lore page: the first 8 bytes of the actor (little-endian)");
    // A chest of r_+01_+02: actor 6b825bfc77b4e23d, the option entity of GatherOption is G (its option is its root).
    const swc::net::ActorId chest = placement("41db9b4e-dae1-3d21-7c39-2f3926babf4c");
    check(hex_actor(chest) == "6b825bfc77b4e23d0000000000000000", "the actor of a chest from its UUID");
    check(swc::net::group_of(chest) == 0x3DE2B477FC5B826BULL, "G of the chest");
    // A capital node of r_+04_+02 (gathering-data.json: template 0, master_plants; root 006a0075af97f6b4, bound entity
    // 006a0074af97f6b4, component 54de589b00fd4609, option 006a0075af97f6b4): target 6f3b96560ea9a534, option entity = G.
    const swc::net::ActorId capital = placement("9b81ddc9-8cf3-0969-81dc-e073815f5646");
    check(hex_actor(capital) == "2b5fac0dba3d5d1a0000000000000000", "the actor of a capital node from its UUID");
    const std::uint64_t capitalGroup = swc::net::group_of(capital);
    check(capitalGroup == 0x1A5D3DBA0DAC5F2BULL, "G of the capital node");
    const swc::objects::BoundIds capitalIds = swc::objects::bound_ids(0x006A0075AF97F6B4ULL, capitalGroup, 0x006A0074AF97F6B4ULL,
        0x54DE589B00FD4609ULL, 0x006A0075AF97F6B4ULL);
    check(capitalIds.target == 0x6F3B96560EA9A534ULL, "the facet target of the capital node: (entity XOR root XOR G) + component");
    check(capitalIds.optionEntity == capitalGroup, "the option entity of the capital node: option XOR root XOR G (= G)");
    // The made-up page of the backend test case IT10 (PAGE_ID 01020304-0506-0708-1112-131415161718): G 0x1010101010101013.
    check(swc::net::group_of(placement("01020304-0506-0708-1112-131415161718")) == 0x1010101010101013ULL, "the actor rule of IT10");
    // A natural node: record 18627 of r_+04_+02 (SingleStone_c of master_minerals: root 00530136db40c5a5, bound entity
    // 00530130db40c5a5, component b0b2bea6067de28a, option 00530136db40c5a5). GAT2: natural_group(4, 2, 18628) = 0x09188221.
    const std::uint64_t natural = swc::objects::natural_group(4, 2, 18627 + 1);
    check(natural == 0x09188221ULL, "the offline id of a natural node (GAT2)");
    check(hex_actor(swc::objects::actor_of_group(natural)) == "21821809000000000000000000000000", "the actor of a natural node: G and 8 zero bytes");
    const swc::objects::BoundIds naturalIds = swc::objects::bound_ids(0x00530136DB40C5A5ULL, natural, 0x00530130DB40C5A5ULL, 0xB0B2BEA6067DE28AULL,
        0x00530136DB40C5A5ULL);
    check(naturalIds.target == 0xB0B2BEAC0F9664ABULL, "the facet target of the natural node");
    check(naturalIds.optionEntity == natural, "the option entity of the natural node (= G)");
    // The fields of G: the counter has 19 bits, the region 5 bits each, the low 3 bits 1.
    check(swc::objects::natural_group(31, 31, 0x7FFFF) == 0xFFFFFFF9ULL, "the largest offline id");
    check(swc::objects::natural_group(-1, 0, 1) == ((1ULL << 13) | (0x1FULL << 3) | 1ULL), "a negative region keeps its low 5 bits");
    check(!swc::objects::parse_uuid("bdf81e34-8c29-69ca-08d8-d51e50b2600").has_value(), "a short UUID does not read");
    check(!swc::objects::parse_uuid("bdf81e34-8c29-69ca-08d8-d51e50b2600z").has_value(), "a UUID with a bad digit does not read");
}

// ---------------------------------------------------------------------------------------------------------- the node record

/** A natural node with a record (the server makes the entity at the first depletion): metadata (master_minerals,
 *  2b9ad6c5-...:2 is not needed: the asset of the test), Interact at slot 4, GatherableController at slot 3. */
void test_node_record() {
    TestClock clock;
    backend::world::Entity node;
    node.kind = backend::world::EntityKind::object;
    const swc::net::ActorId actor = swc::objects::actor_of_group(0x09188221ULL);
    for (std::size_t index = 0; index < actor.size(); ++index) {
        node.actor.bytes[index] = actor[index];
    }
    node.id = backend::world::entity_group_id(node.actor);
    node.object.subId = 2;
    node.object.bounds.push_back(backend::world::ObjectBound{4, true, 0});
    node.object.nodeSlot = 3;
    node.object.nodeState = swc::objects::kNodeDepleted;
    rep::PartsRequest request;
    request.clock = &clock;
    decoder::Record record;
    record.interest = 90;
    record.fragments.push_back(fragment_of(0, 10, rep::encode_metadata_state(node, rep::View::observer, clock), "metadata (node)"));
    for (const rep::SlotState& state : rep::encode_interact_states(node, rep::View::observer, request)) {
        record.fragments.push_back(fragment_of(state.slot, 2930, state.state, "interact (node)"));
    }
    record.fragments.push_back(fragment_of(3, 12, rep::encode_gatherable_state(node, rep::View::observer, clock), "gatherable (depleted)"));
    swc::creatures::EntityTable table;
    table.apply(record, 0);
    const swc::creatures::Entity* seen = table.find(90);
    check(seen != nullptr && seen->hasMetadata && swc::net::group_of(seen->actor) == 0x09188221ULL, "the record of the node: its actor gives G");
    if (seen == nullptr) {
        return;
    }
    check(seen->hasGatherable && seen->gatherableState == swc::objects::kNodeDepleted && seen->gatherableSlot == 3, "the node state Depleted at slot 3");
    check(seen->interact.count(4) == 1 && seen->interact.at(4).enabled, "the Interact state of the node (slot 4)");
    check(!seen->hasPosition() && !seen->hasWorldPlace, "a node record has no place: the creature view makes no actor for it");
    const std::uint32_t revision = seen->gatherableRevision;
    node.object.nodeState = swc::objects::kNodeHealthy;
    decoder::Record respawn;
    respawn.interest = 90;
    respawn.fragments.push_back(fragment_of(3, 12, rep::encode_gatherable_state(node, rep::View::observer, clock), "gatherable (healthy)"));
    table.apply(respawn, 0);
    seen = table.find(90);
    check(seen != nullptr && seen->gatherableState == swc::objects::kNodeHealthy && seen->gatherableRevision == revision + 1,
          "the respawn: the node state Healthy (the record stays)");
}

// ---------------------------------------------------------------------------------------------------- the Achievement state

void test_achievements() {
    TestClock clock;
    backend::world::Entity player;
    player.kind = backend::world::EntityKind::player;
    player.achievements = {3, 778, 1000};   // 778: the bit of the lore page above
    const backend::server::hub::Bytes state = rep::encode_achievement_state(player, rep::View::owner, clock);
    check(!state.empty(), "the backend sends the Achievement state to the owner");
    const decoder::Fragment fragment = fragment_of(swc::net::kSlotAchievement, swc::net::kStateAchievement.index, state, "achievement");
    const auto bytes = swc::net::read_bit_vector(decoder::field_of(fragment, 0, 0));
    check(bytes.has_value() && bytes->size() == 1000 / 8 + 1, "the byte vector of the Achievement state (full form)");
    if (!bytes) {
        return;
    }
    check(swc::net::has_bit(*bytes, 3) && swc::net::has_bit(*bytes, 778) && swc::net::has_bit(*bytes, 1000), "the bits of the achievements");
    check(!swc::net::has_bit(*bytes, 777) && !swc::net::has_bit(*bytes, 4) && !swc::net::has_bit(*bytes, 5000), "no other bit");
    check(rep::encode_achievement_state(player, rep::View::observer, clock).empty(), "no Achievement state for an observer");
}

// ---------------------------------------------------------------------------------------------------- the Gathering state

/** @return The fragment of the Gathering state of the backend for a character with this progress. */
decoder::Fragment gathering_fragment(const backend::world::GatherProgress& progress, const TestClock& clock, const char* name) {
    backend::world::Entity player;
    player.kind = backend::world::EntityKind::player;
    player.gathering = progress;
    rep::PartsRequest request;
    request.clock = &clock;
    const std::vector<backend::server::hub::Bytes> parts = rep::encode_gathering_state(player, rep::View::owner, request);
    check(parts.size() == 1, std::string(name) + ": one Gathering state for the owner");
    return fragment_of(swc::net::kSlotGathering, swc::net::kStateGathering.index, parts.empty() ? backend::server::hub::Bytes{} : parts.front(), name);
}

void test_gathering_state() {
    TestClock clock;
    // The start of the work of a node: Success, 35 s of BaseGatherTime, the speed 6.25 of a tool, the G of the node (gathering.md).
    backend::world::GatherProgress progress;
    progress.status = 1;
    progress.remaining = 35.0F;
    progress.speed = 6.25F;
    progress.gatherers = 1;
    progress.reference = 1;
    progress.active = true;
    progress.target = 0x09188221ULL;
    progress.syncReference = 1;
    swc::net::GatheringState state;
    check(swc::net::apply_gathering(gathering_fragment(progress, clock, "gathering (success)"), state), "the first Gathering state changes the state");
    check(state.status == swc::net::kGatherSuccess && state.remaining == 35.0F && state.speed == 6.25F && state.gatherers == 1 && state.reference == 1,
          "fields 0 to 4 of the Gathering state (big-endian)");
    check(state.active && state.target == 0x09188221ULL && state.syncReference == 1, "fields 6 to 8: the G of the node");
    // The rule of the client: fields 0 to 3 change only with a new reference (field 4), fields 6 and 7 only with field 8.
    backend::world::GatherProgress same = progress;
    same.status = 3;
    same.active = false;
    check(!swc::net::apply_gathering(gathering_fragment(same, clock, "gathering (same references)"), state) && state.status == swc::net::kGatherSuccess
              && state.active,
          "a state with the same references changes nothing");
    backend::world::GatherProgress done = progress;
    done.status = 3;
    done.remaining = 0.0F;
    done.reference = 2;
    done.active = false;
    done.target = 0x00000000FFFFFFFFULL;
    done.syncReference = 2;
    check(swc::net::apply_gathering(gathering_fragment(done, clock, "gathering (done)"), state) && state.status == swc::net::kGatherDone && !state.active
              && state.target == 0x00000000FFFFFFFFULL,
          "the completion: Done, no target");
    // No Gathering state before the first work (reference 0).
    backend::world::Entity idle;
    idle.kind = backend::world::EntityKind::player;
    rep::PartsRequest request;
    request.clock = &clock;
    check(rep::encode_gathering_state(idle, rep::View::owner, request).empty(), "no Gathering state before the first work");
}

void test_gather_watch() {
    using Step = swc::interact::GatherWatch::Step;
    swc::net::GatheringState state;
    state.reference = 4;   // an earlier work
    state.status = swc::net::kGatherDone;
    swc::interact::GatherWatch watch;
    watch.start(10.0, state);
    check(watch.update(10.2, state) == Step::waiting, "the Done of an earlier work does not end the new use");
    // Success 0.6 s after the press (the hold 0.5 s of GatherOption, then the request): 35 s / 6.25 = 5.6 s of work.
    state.reference = 5;
    state.status = swc::net::kGatherSuccess;
    state.remaining = 35.0F;
    state.speed = 6.25F;
    check(watch.update(10.6, state) == Step::running && std::fabs(watch.end_time() - 16.2) < 1e-6, "the time of the work: remaining / speed");
    check(std::fabs(watch.progress(13.4) - 0.5F) < 1e-5F, "the progress of the work");
    check(watch.update(17.0, state) == Step::running, "the request stays until the end (no release at the time)");
    state.reference = 6;
    state.status = swc::net::kGatherDone;
    check(watch.update(17.1, state) == Step::done, "Done ends the use (the client sends 2473)");
    check(watch.update(30.0, state) == Step::done, "the end stays");
    // A supply chest: 2.5 s, speed 1; no Done: the use ends kSlack after the time.
    swc::interact::GatherWatch chest;
    chest.start(0.0, state);
    state.reference = 7;
    state.status = swc::net::kGatherSuccess;
    state.remaining = 2.5F;
    state.speed = 1.0F;
    check(chest.update(0.7, state) == Step::running && chest.update(5.1, state) == Step::running && chest.update(5.3, state) == Step::late,
          "no end by the time of the work plus 2 s: late");
    // Failure (a walk out of the range, damage).
    swc::interact::GatherWatch failed;
    failed.start(0.0, state);
    state.reference = 8;
    state.status = swc::net::kGatherFailure;
    check(failed.update(1.0, state) == Step::failed, "Failure ends the use");
    // No Success after the press.
    swc::interact::GatherWatch none;
    none.start(0.0, state);
    check(none.update(4.9, state) == Step::waiting && none.update(5.1, state) == Step::late, "no work 5 s after the press: late");
}

// -------------------------------------------------------------------------------------------------------- the node file

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int index = 0; index < 4; ++index) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
    }
}

void put_float(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    put32(out, bits);
}

void test_node_file() {
    // Region r_+04_+02 with side 32: one node in cell (2, 0), two in cell (0, 1).
    const std::uint32_t side = 32;
    std::vector<std::uint8_t> file = {'S', 'W', 'O', 'N'};
    put32(file, 1);
    put32(file, 4);
    put32(file, 2);
    put32(file, 3);
    put32(file, side);
    std::vector<std::uint32_t> offsets(side * side + 1, 3);
    for (std::uint32_t cell = 0; cell <= 2; ++cell) {
        offsets[cell] = 0;
    }
    for (std::uint32_t cell = 3; cell <= side; ++cell) {
        offsets[cell] = 1;
    }
    for (const std::uint32_t offset : offsets) {
        put32(file, offset);
    }
    const auto node = [&file](std::uint32_t record, std::uint16_t type, float x, float y, float z) {
        put32(file, record);
        file.push_back(static_cast<std::uint8_t>(type));
        file.push_back(static_cast<std::uint8_t>(type >> 8));
        file.push_back(0);
        file.push_back(0);
        put_float(file, x);
        put_float(file, y);
        put_float(file, z);
    };
    node(18627, 120, 8320.0F + 130.0F, 4096.0F + 10.0F, 135.9F);
    node(5, 7, 8192.0F + 3.0F, 4096.0F + 70.0F, 120.0F);
    node(6, 7, 8192.0F + 4.0F, 4096.0F + 71.0F, 121.0F);
    swc::objects::NodeFile nodes;
    check(nodes.read(file) && nodes.size() == 3 && nodes.regionX() == 4 && nodes.regionY() == 2, "the node file reads");
    const auto first = nodes.cell(2, 0);
    check(first.size() == 1 && first[0].record == 18627 && first[0].type == 120, "the node of cell (2, 0)");
    check(nodes.cell(0, 1).size() == 2 && nodes.cell(0, 1)[1].record == 6, "the two nodes of cell (0, 1)");
    check(nodes.cell(1, 0).empty() && nodes.cell(-1, 0).empty() && nodes.cell(32, 0).empty(), "an empty cell and the cells outside");
    check(swc::objects::grid_cell(8450.0) == 132 && swc::objects::grid_cell(-0.5) == -1 && swc::objects::grid_cell(64.0) == 1, "the grid cell of a place");
    std::vector<std::uint8_t> shorter(file.begin(), file.end() - 1);
    check(!nodes.read(shorter), "a file with a missing byte does not read");
    std::vector<std::uint8_t> other = file;
    other[4] = 2;
    check(!nodes.read(other), "a file of another format does not read");
}

} // namespace

int main() {
    test_ids();
    test_node_record();
    test_achievements();
    test_gathering_state();
    test_gather_watch();
    test_node_file();
    std::printf("objects: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
