// The quest wire of the client (Linear AST-236; protocol notes 4.53 and 4.54; docs/QUESTS.md): the reader of the Objectives state (slot 22,
// type 3857) and of the pin readiness (slot 73, type 5606) against the states that the backend encoder makes (its core library,
// server/rep/states/objectives_state.cpp), and the request bodies of the client (2709, 1458, 2617, 3836, 5600, 2297, 6416) against the
// readers of the backend (server/rep/quest_messages.cpp). The quest data is the quest-data.json of the backend copy of the client (the
// tree of 06_getoffbeach and the other objectives of the game data). The cases: the start quest of a new character (the start states of
// live check L1 of stage 1.8a), a large list (60 objectives, 8 tracked, many timers), an empty list after a list, a state that the reader
// must refuse (a cut byte), and two owners that keep their own lists. Build and run: tools/test-net.sh.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include "Net/Codec/SwcRepClient.h"
#include "Net/Codec/SwcRepMessages.h"
#include "gamedata/quest_data.h"
#include "server/rep/conversation_messages.h"
#include "server/rep/lock_messages.h"
#include "server/rep/progress_requests.h"
#include "server/rep/quest_messages.h"
#include "server/rep/states/state_codecs.h"
#include "support/manual_clock.h"
#include "support/state_decoder.h"

namespace {

namespace backend = soulwarden::backend;
namespace decoder = backend::tests;
using swc::net::RepClient;

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& text)
{
	++g_checks;
	if (!condition) {
		++g_failures;
	}
	std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", text.c_str());
}

/** @return The fragment of a state of the backend encoder: the vendored reader must take all its bytes. */
decoder::Fragment fragment_of(const std::vector<std::uint8_t>& bytes, std::uint32_t slot, std::uint32_t type, bool& whole)
{
	decoder::Fragment fragment;
	fragment.slot = slot;
	fragment.type = type;
	std::size_t position = 0;
	whole = decoder::detail::read_state(bytes, position, fragment) && position == bytes.size();
	return fragment;
}

/** A quest of the backend world with the task states of its tree (each node: the given state and progress). */
backend::world::QuestInstance quest_of(const backend::gamedata::QuestObjective& objective, std::uint64_t instance, std::uint8_t state)
{
	backend::world::QuestInstance quest;
	quest.instance = instance;
	quest.objective = objective.id;
	quest.objectiveCrc = objective.crc;
	quest.originTerritory = 6;
	quest.tasks.resize(objective.nodes.size());
	for (auto& task : quest.tasks) {
		task.state = state;
	}
	return quest;
}

std::size_t elements_of(const backend::gamedata::QuestObjective& objective)
{
	std::size_t count = 0;
	for (const auto& node : objective.nodes) {
		count += node.task != backend::gamedata::kNoTask ? 1 : 0;
	}
	return count;
}

void objectives_state(const backend::gamedata::QuestData& data)
{
	std::printf("the Objectives state (3857) of the backend encoder:\n");
	backend::tests::ManualClock clock;
	const backend::gamedata::QuestObjective* beach = data.objective("06_getoffbeach");
	check(beach != nullptr && beach->crc == 0xE4D66020U, "06_getoffbeach has the CRC E4D66020 (protocol notes 4.53)");
	if (beach == nullptr) {
		return;
	}
	// The start quest of a new character: the start states of live check L1 (indexes 0, 2, 3, 5, 6: 2/1; 1: 3/1; 4: 3/4; 7, 8, 9, 14, 15:
	// 1/0; 10 to 13: 0/0; 16: 1/3), active, tracked, and one timer for the case of field 0.
	backend::world::Entity own;
	own.quests.loaded = true;
	own.quests.data = &data;
	backend::world::QuestInstance start = quest_of(*beach, 0x1122334455667788ULL, 0);
	const std::uint8_t states[17][2] = {{2, 1}, {3, 1}, {2, 1}, {2, 1}, {3, 4}, {2, 1}, {2, 1}, {1, 0}, {1, 0}, {1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
		{1, 0}, {1, 0}, {1, 3}};
	for (std::size_t index = 0; index < start.tasks.size() && index < 17; ++index) {
		start.tasks[index].state = states[index][0];
		start.tasks[index].progress = states[index][1];
	}
	start.tasks[9].startNs = 1792600000123456789ULL;
	own.quests.active.push_back(start);
	own.quests.tracked.push_back(start.instance);
	const backend::server::hub::Bytes bytes = backend::server::rep::encode_objectives_state(own, backend::server::rep::View::owner, clock);
	bool whole = false;
	const decoder::Fragment fragment = fragment_of(bytes, swc::net::kSlotObjectives, swc::net::kStateObjectives.index, whole);
	check(whole, "the vendored reader takes the whole state (" + std::to_string(bytes.size()) + " bytes)");
	RepClient::OwnEntity client;
	check(RepClient::read_objectives(fragment, client), "the client reads the start state");
	check(client.questActive.size() == 1 && client.questActive[0].objective == 0xE4D66020U && client.questActive[0].instance == 0x1122334455667788ULL,
		"one active objective: 06_getoffbeach with the full u64 instance");
	check(client.questActive.size() == 1 && client.questActive[0].originTerritory == 6 && client.questActive[0].available,
		"the origin territory 6, available");
	check(client.questTracked == std::vector<std::uint64_t>{0x1122334455667788ULL}, "the tracked list has the instance");
	check(client.questTasks.size() == elements_of(*beach), "one task element for each task of the tree (" + std::to_string(client.questTasks.size()) + ")");
	bool order = true;
	for (std::size_t index = 1; index < client.questTasks.size(); ++index) {
		order = order && client.questTasks[index - 1].index < client.questTasks[index].index;
	}
	check(order, "the task elements are in the index order of the tree (a child before its container)");
	bool values = !client.questTasks.empty();
	for (const auto& task : client.questTasks) {
		const auto& node = beach->nodes[task.index];
		values = values && task.state == states[task.index][0] && task.progress == states[task.index][1]
			&& task.task == data.task(node.task).crc && task.instance == start.instance;
	}
	check(values, "each element has its index, the task CRC of the data, the progress, and the state");
	check(client.questStarts.size() == 1 && client.questStarts[0].index == 9 && client.questStarts[0].startNs == 1792600000123456789ULL,
		"the timer map has the start of task 9 in Unix ns");

	// A large list: 60 objectives of the data with their full trees, 8 tracked, a timer at each task 0.
	backend::world::Entity large;
	large.quests.loaded = true;
	large.quests.data = &data;
	std::size_t expectedTasks = 0;
	for (const auto& objective : data.objectives()) {
		if (large.quests.active.size() == 60) {
			break;
		}
		if (objective.nodes.empty()) {
			continue;
		}
		backend::world::QuestInstance quest = quest_of(objective, 0xF000000000000000ULL + large.quests.active.size() * 0x100000001ULL, 1);
		quest.tasks[0].startNs = 1792600000000000000ULL + large.quests.active.size();
		expectedTasks += elements_of(objective);
		large.quests.active.push_back(quest);
		if (large.quests.tracked.size() < 8) {
			large.quests.tracked.push_back(quest.instance);
		}
	}
	const backend::server::hub::Bytes largeBytes = backend::server::rep::encode_objectives_state(large, backend::server::rep::View::owner, clock);
	const decoder::Fragment largeFragment = fragment_of(largeBytes, swc::net::kSlotObjectives, swc::net::kStateObjectives.index, whole);
	check(whole, "a large state: the vendored reader takes it whole (" + std::to_string(largeBytes.size()) + " bytes)");
	RepClient::OwnEntity largeClient;
	check(RepClient::read_objectives(largeFragment, largeClient) && largeClient.questActive.size() == 60 && largeClient.questTracked.size() == 8
			  && largeClient.questTasks.size() == expectedTasks && largeClient.questStarts.size() == 60,
		"a large state: 60 active, 8 tracked, " + std::to_string(expectedTasks) + " task elements, 60 timers");
	check(largeClient.questActive.size() == 60 && largeClient.questActive[59].instance == 0xF000000000000000ULL + 59 * 0x100000001ULL,
		"a large state: the last instance keeps its high bits");

	// An empty list after a list: each field replaces its list.
	backend::world::Entity empty;
	empty.quests.loaded = true;
	empty.quests.data = &data;
	const backend::server::hub::Bytes emptyBytes = backend::server::rep::encode_objectives_state(empty, backend::server::rep::View::owner, clock);
	const decoder::Fragment emptyFragment = fragment_of(emptyBytes, swc::net::kSlotObjectives, swc::net::kStateObjectives.index, whole);
	check(whole && RepClient::read_objectives(emptyFragment, largeClient) && largeClient.questActive.empty() && largeClient.questTracked.empty()
			  && largeClient.questTasks.empty() && largeClient.questStarts.empty(),
		"an empty state after the large state: all the lists are empty (" + std::to_string(emptyBytes.size()) + " bytes)");

	// A hand-in (backend stage 1.8b): the record that removes the instance names it in field 2 (completedObjectives), written only with
	// an instance; the client then shows a completion, not "Objective Failed".
	backend::world::Entity handIn;
	handIn.quests.loaded = true;
	handIn.quests.data = &data;
	handIn.quests.completed.push_back(0x1122334455667788ULL);
	const backend::server::hub::Bytes handInBytes = backend::server::rep::encode_objectives_state(handIn, backend::server::rep::View::owner, clock);
	const decoder::Fragment handInFragment = fragment_of(handInBytes, swc::net::kSlotObjectives, swc::net::kStateObjectives.index, whole);
	RepClient::OwnEntity afterHandIn = client;
	check(whole && RepClient::read_objectives(handInFragment, afterHandIn) && afterHandIn.questActive.empty()
			  && afterHandIn.questCompleted == std::vector<std::uint64_t>{0x1122334455667788ULL},
		"a hand-in: field 2 names the completed instance, the active list is empty (" + std::to_string(handInBytes.size()) + " bytes)");

	// A cut state: the reader keeps the lists of the client.
	decoder::Fragment cut = fragment;
	auto& tasks = cut.fields[{1, 4}];
	tasks.pop_back();
	RepClient::OwnEntity kept = client;
	check(!RepClient::read_objectives(cut, kept) && kept.questTasks.size() == client.questTasks.size(), "a cut task list is refused: no change");

	// Two owners: each client keeps the list of its own state.
	RepClient::OwnEntity first;
	RepClient::OwnEntity second;
	check(RepClient::read_objectives(fragment, first) && RepClient::read_objectives(largeFragment, second)
			  && first.questActive.size() == 1 && second.questActive.size() == 60,
		"two owners: each one has its own list");
	const backend::server::hub::Bytes observer = backend::server::rep::encode_objectives_state(own, backend::server::rep::View::observer, clock);
	check(observer.empty(), "the backend sends no Objectives state to an observer");

	// The pin readiness (5606): group 0 field 0, one byte 1.
	const backend::server::hub::Bytes ready = backend::server::rep::encode_seasons_state(own, backend::server::rep::View::owner, clock);
	const decoder::Fragment readyFragment = fragment_of(ready, swc::net::kSlotPinReadiness, swc::net::kStatePinReadiness.index, whole);
	const auto value = decoder::field_of(readyFragment, 0, 0);
	check(whole && value.size() == 1 && value[0] == 1, "the pin readiness (5606): initialized 1");
}

void requests()
{
	std::printf("the requests of the client against the readers of the backend:\n");
	const std::uint64_t group = 0x0000000064B6021ULL;
	std::vector<bool> flags(swc::net::kAutoPinFlags, true);
	flags[swc::net::kCommunityGoalType] = false;
	const auto pinning = swc::net::auto_pinning_body(group, flags);
	const auto pin = backend::server::rep::read_auto_pinning(pinning);
	check(pinning.size() == 53 && pin && pin->flags.size() == 36 && !pin->flags[6] && pin->flags[0],
		"2709: 53 bytes, 36 flags, CommunityGoal off (live check L1)");
	check(pin && pin->target == group + swc::net::kObjectivesComponent && pin->entity == group, "2709: the target is G + the Objectives component");
	const std::vector<std::uint64_t> tracked{0x1122334455667788ULL, 7, 8, 9, 10, 11, 12, 13, 14};
	const auto pinned = backend::server::rep::read_tracked_objectives(swc::net::tracked_objectives_body(group, tracked));
	check(pinned && pinned->instances.size() == 8 && pinned->instances[0] == 0x1122334455667788ULL, "1458: at most 8 instances, full u64");
	const auto none = backend::server::rep::read_tracked_objectives(swc::net::tracked_objectives_body(group, {}));
	check(none && none->instances.empty(), "1458: the empty list");
	const auto screen = backend::server::rep::read_objective_call(swc::net::open_ui_screen_body(group, 0xA1126B78U), false);
	check(screen && screen->value == 0xA1126B78U && screen->target == group + swc::net::kObjectivesComponent, "2617: the screen CRC of \"hud\"");
	const auto abandon = backend::server::rep::read_objective_call(swc::net::objective_instance_body(group, 0x1122334455667788ULL), true);
	check(abandon && abandon->value == 0x1122334455667788ULL, "3836 and 5600: the instance");
	const std::array<std::uint8_t, 16> actor{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
	const auto entered = backend::server::rep::read_territory_report(
		swc::net::territory_report_body(group, actor, 0x10, 0x20, 1203, 0xD66D9B33U), true);
	check(entered && entered->territory == 1203 && entered->achievement == 0xD66D9B33U
			  && entered->target == group + swc::net::kTerritoryDetectorComponent,
		"2297: the territory and the achievement CRC");
	const auto left = backend::server::rep::read_territory_report(swc::net::territory_report_body(group, actor, 0x10, 0x20, 1203, std::nullopt), false);
	check(left && left->territory == 1203 && left->achievement == 0, "6416: the territory, no achievement");
	// The place cue of POI 40142 (AST-241; research poi-cues.md): the reference of the Territory component of its placement
	// (222840c7-9fef-93ad-170c-b720ad0ad6ab: the actor of the UUID rule, group, local = group + 0x4FB021A86DF7AE7A), big-endian group and local.
	const std::array<std::uint8_t, 16> poiActor{0x03, 0x45, 0xe5, 0x32, 0xe7, 0xf7, 0x24, 0x35, 0, 0, 0, 0, 0, 0, 0, 0};
	const auto poiBody = swc::net::territory_report_body(group, poiActor, 0x3524f7e732e54503ULL, 0x84d5198fa0dcf37dULL, 40142, 0xD66D9B33U);
	std::string poiHex;
	for (std::size_t index = 16; index < 48 && index < poiBody.size(); ++index) {
		char pair[3];
		std::snprintf(pair, sizeof pair, "%02x", poiBody[index]);
		poiHex += pair;
	}
	check(poiBody.size() == 54 && poiHex == "0345e532e7f7243500000000000000003524f7e732e5450384d5198fa0dcf37d"
			  && poiBody[48] == 0x9c && poiBody[49] == 0xce,
		"2297 of POI 40142: 54 bytes, the actor raw, group and local big-endian, the territory 9c ce");
	const auto zeroCue = backend::server::rep::read_territory_report(
		swc::net::territory_report_body(group, poiActor, 0x3524f7e732e54503ULL, 0x84d5198fa0dcf37dULL, 40142, 0U), true);
	check(zeroCue && zeroCue->achievement == 0 && zeroCue->territory == 40142, "2297 with the achievement 0: 54 bytes, the backend reads it");
	// The topics of a person conversation (AST-248): the Conversation facet of the player root.
	const auto topic = backend::server::rep::read_conversation_topic(swc::net::conversation_topic_body(group, 0xE532485AU));
	check(topic && topic->topics.size() == 1 && topic->topics[0] == 0xE532485AU && topic->target == group + swc::net::kConversationComponent
			  && topic->entity == group,
		"3158: the topic e532485a (live check L3 of the backend stage 2.5c) on the Conversation facet");
	const auto finished = backend::server::rep::read_finished_topics(swc::net::finished_topics_body(group, {0xE532485AU, 2, 3}));
	check(finished && finished->topics.size() == 3 && finished->topics[0] == 0xE532485AU, "5397: the finished topics");
	std::vector<std::uint32_t> many(12, 7);
	const auto limit = backend::server::rep::read_finished_topics(swc::net::finished_topics_body(group, many));
	check(limit && limit->topics.size() == 10, "5397: at most 10 topics are written");
	// The offer and the hand-in at a person (AST-239): the Conversation facet, the CRC-32 of the lower-case ObjectiveID (live I2 of the
	// backend stage 1.8b: d137c605 06_skinningknife; live P1: e4d66020 06_getoffbeach). The backend reads them as 6703 (one u32).
	const auto accept = backend::server::rep::read_cutscene_request(swc::net::conversation_objective_body(group, 0xD137C605U));
	check(accept && accept->cutscene == 0xD137C605U && accept->target == group + swc::net::kConversationComponent && accept->entity == group
			  && swc::net::conversation_objective_body(group, 0xD137C605U).size() == 20,
		"3772: 20 bytes, the objective d137c605 (06_skinningknife) on the Conversation facet");
	const auto handIn = backend::server::rep::read_cutscene_request(swc::net::conversation_objective_body(group, 0xE4D66020U));
	check(handIn && handIn->cutscene == 0xE4D66020U && handIn->target == group + swc::net::kConversationComponent,
		"794: the objective e4d66020 (06_getoffbeach) on the Conversation facet");
	// The cutscene of a quest task (AST-239, the check aid): 6703 and 6706 on the PlayerCutscene facet; 6707 from the backend writer.
	const auto request = backend::server::rep::read_cutscene_request(swc::net::cutscene_body(group, 0x4817DD08U));
	check(request && request->cutscene == 0x4817DD08U && request->target == group + swc::net::kPlayerCutsceneComponent && request->entity == group,
		"6703 and 6706: the beach cutscene 4817dd08 on the PlayerCutscene facet");
	const auto grantBytes = backend::server::rep::cutscene_granted_body(group, 0x4817DD08U);
	const auto grant = swc::net::read_cutscene_grant(grantBytes);
	check(grant && grant->cutscene == 0x4817DD08U && grant->entity == group && grant->target == group + swc::net::kPlayerCutsceneComponent,
		"6707: the client reads the grant of the backend writer (4817dd08)");
	check(!swc::net::read_cutscene_grant(std::vector<std::uint8_t>(grantBytes.begin(), grantBytes.end() - 1)),
		"6707: a short body is not a grant");

	// Stage Q5 (AST-240): the cinematic TempestRising (0x40214776, quests-18e cinematic ANSWERS.md Q3): 6606 and 6542 with the same body,
	// read by the backend reader; a whisper 5070 from the backend writer (live W1 of 1.8b: 01_Whisper_Beach_05, 69128a14); 6708 [I: no
	// backend writer] in the no-suffix form of 6709 (the backend writer of 6709).
	const auto cinematic = backend::server::rep::read_cutscene_request(swc::net::cinematic_body(group, 0x40214776U));
	check(cinematic && cinematic->cutscene == 0x40214776U && cinematic->target == group + swc::net::kPlayerCutsceneComponent
			&& cinematic->entity == group,
		"6606 and 6542: the cinematic tempestrising 40214776 on the PlayerCutscene facet");
	const std::uint64_t whisperTarget = group + 0x7D5CA09D7B3CC6FAULL;
	const auto whisperBytes = backend::server::rep::whisper_body(whisperTarget, group, 0x69128A14U);
	const auto whisper = swc::net::read_whisper_start(whisperBytes);
	check(whisper && whisper->whisper == 0x69128A14U && whisper->entity == group && whisper->target == whisperTarget,
		"5070: the client reads the whisper of the backend writer (01_whisper_beach_05, 69128a14)");
	check(!swc::net::read_whisper_start(std::vector<std::uint8_t>(whisperBytes.begin(), whisperBytes.end() - 1)),
		"5070: a short body is not a whisper");
	const auto cancelBytes = backend::server::rep::cutscene_finalized_body(group);
	const auto cancel = swc::net::read_component_call(cancelBytes);
	check(cancel && cancel->entity == group && cancel->target == group + swc::net::kPlayerCutsceneComponent,
		"6708 [I]: the no-suffix form of the PlayerCutscene facet (the bytes of the backend writer of 6709)");
}

} // namespace

/** QG5 (protocol notes 4.62): the states of an encounter root and a start object of the backend encoders, and the link of field 5. */
// The factions (QG7, 2a F1 and F3): the Faction state of the backend encoder for the owner and an observer, the empty Social state, and the
// body of 3161 through the reader of the backend.
void faction_states()
{
	std::printf("faction states (QG7)\n");
	backend::tests::ManualClock clock;
	namespace rep = backend::server::rep;
	backend::world::Entity character;
	character.kind = backend::world::EntityKind::player;
	// A character with no faction: no owner fragment in its first record.
	check(rep::encode_faction_state(character, rep::View::owner, rep::PartsRequest{}).empty(), "no Faction fragment for a character with no faction");
	// A join: the Syndicate, the time 0, the count 0 (the owner bytes 06 18 <8> <2> 01 01).
	character.faction.faction = 1;
	const auto owner = rep::encode_faction_state(character, rep::View::owner, rep::PartsRequest{});
	bool whole = false;
	check(owner.size() == 1, "one owner part");
	decoder::Fragment fragment = fragment_of(owner.front(), swc::net::kSlotFaction, swc::net::kStateFaction.index, whole);
	check(whole, "the vendored reader takes the owner Faction state (" + std::to_string(owner.front().size()) + " bytes)");
	RepClient::OwnEntity own;
	check(RepClient::apply_faction(fragment, own) && own.hasFaction && own.faction == 1 && own.factionChangeNs == 0 && own.factionChangeCount == 0
	          && own.factionRevision == 1, "the join: the Syndicate, the time 0, the count 0");
	// The same answer again (a refused request): the revision moves (the retail apply republishes the faction).
	check(RepClient::apply_faction(fragment, own) && own.faction == 1 && own.factionRevision == 2, "the same faction again adds a revision");
	// A change: the Marauders, the time of now, the count 1.
	character.faction.faction = 2;
	character.faction.changedNs = 1793000000123456789ULL;
	character.faction.changes = 1;
	const auto changed = rep::encode_faction_state(character, rep::View::owner, rep::PartsRequest{});
	fragment = fragment_of(changed.front(), swc::net::kSlotFaction, swc::net::kStateFaction.index, whole);
	check(RepClient::apply_faction(fragment, own) && own.faction == 2 && own.factionChangeNs == 1793000000123456789ULL && own.factionChangeCount == 1,
	      "the change: the Marauders, the time in Unix nanoseconds, the count 1");
	// An observer gets only group 2: the time and the count of an own entity stay.
	character.faction.publicChanged = 1;
	const auto observer = rep::encode_faction_state(character, rep::View::observer, rep::PartsRequest{});
	check(observer.size() == 1, "one observer part");
	fragment = fragment_of(observer.front(), swc::net::kSlotFaction, swc::net::kStateFaction.index, whole);
	check(whole && RepClient::apply_faction(fragment, own) && own.faction == 2 && own.factionChangeCount == 1, "the observer part: the faction only");
	// The empty Social state (31 bytes with the slot and the type).
	const backend::server::hub::Bytes social = rep::encode_social_state(character, rep::View::owner, clock);
	fragment_of(social, swc::net::kSlotSocial, swc::net::kStateSocial.index, whole);
	check(whole, "the vendored reader takes the empty Social state (" + std::to_string(social.size()) + " bytes)");
	// 3161: the Faction facet of the entity Social (the rule of 4.2 with the ids of slice-ids.json) and the faction.
	const std::uint64_t group = 0x4C554F5340000100ULL;
	const backend::server::hub::Bytes body = swc::net::set_faction_body(group, 3);
	const auto request = rep::read_set_faction_request(body);
	check(body.size() == 17 && request.has_value(), "the body of 3161 has 17 bytes and the reader of the backend takes it");
	check(request && request->call.entity == group && request->faction == 3
	          && request->call.target == ((0xE8A82A615858169DULL ^ 0xDAEE4E2555650AB7ULL ^ group) + 0xE9FFAD8E007C4913ULL),
	      "3161: the entity G, the Faction facet of the entity Social, the Covenant");
}

void board_states()
{
	std::printf("board states (C-12: QG6a, QG6b)\n");
	backend::tests::ManualClock clock;
	namespace rep = backend::server::rep;
	// The provider state of a character after the open of the Windsward board: the claim of territory 6, the expiration, a town entry with a
	// hand-in of this seed; then a faction board with three offers (the second accepted: its instance, available 0).
	backend::world::Entity character;
	character.kind = backend::world::EntityKind::player;
	check(rep::encode_objective_interactor_state(character, rep::View::owner, clock).empty(), "no provider fragment before the first open");
	character.missionBoard.opened = true;
	const std::array<std::uint8_t, 16> claim{0x4b, 0x65, 0x7e, 0x1a, 0x68, 0xe0, 0x6c, 0x9f};
	character.missionBoard.provider = claim;
	character.missionBoard.expirationNs = 1'800'000'000'000ULL;
	character.missionBoard.towns[6].seed = 0x12345678U;
	character.missionBoard.towns[6].unavailable = {0xAAAA0001U};
	character.missionBoard.towns[6].previousProjects = {0x2591C412U};
	bool whole = false;
	auto bytes = rep::encode_objective_interactor_state(character, rep::View::owner, clock);
	auto fragment = fragment_of(bytes, swc::net::kSlotObjectiveInteractor, swc::net::kStateObjectiveInteractor.index, whole);
	check(whole, "the vendored reader takes the provider state of a town board (" + std::to_string(bytes.size()) + " bytes)");
	RepClient::OwnEntity own;
	check(RepClient::apply_provider(fragment, own) && own.hasProvider && own.provider == claim && own.providerExpirationNs == 1'800'000'000'000ULL
	          && own.boardOffers.empty() && own.boardTowns.size() == 1 && own.providerRevision == 1,
	      "the town board: the claim, the expiration, no explicit offer, one town entry");
	check(!own.boardTowns.empty() && own.boardTowns[0].territory == 6 && own.boardTowns[0].seed == 0x12345678U
	          && own.boardTowns[0].unavailable == std::vector<std::uint32_t>{0xAAAA0001U}
	          && own.boardTowns[0].previousProjects == std::vector<std::uint32_t>{0x2591C412U},
	      "the town entry of Windsward: the seed, the unavailable mission, the previous project");
	character.missionBoard.faction = true;
	for (std::uint32_t index = 0; index < 3; ++index) {
		backend::world::MissionOffer offer;
		offer.mission = 0xBEEF0000U + index;
		offer.destination = claim;
		offer.origin = 4;
		offer.available = index != 1;
		offer.instance = index == 1 ? 0x4C554F5340000042ULL : 0;
		character.missionBoard.offers.push_back(offer);
	}
	bytes = rep::encode_objective_interactor_state(character, rep::View::owner, clock);
	fragment = fragment_of(bytes, swc::net::kSlotObjectiveInteractor, swc::net::kStateObjectiveInteractor.index, whole);
	check(whole && RepClient::apply_provider(fragment, own) && own.boardOffers.size() == 3, "the faction board: three explicit offers");
	check(own.boardOffers.size() == 3 && own.boardOffers[1].mission == 0xBEEF0001U && own.boardOffers[1].instance == 0x4C554F5340000042ULL
	          && !own.boardOffers[1].available && own.boardOffers[0].available && own.boardOffers[2].origin == 4
	          && own.boardOffers[0].destination == claim,
	      "the offers in the order of 3816: the accepted one with its instance and available 0");
	check(rep::encode_objective_interactor_state(character, rep::View::observer, clock).empty(), "an observer gets no provider state");
	// The daily bonuses (GameEvent, slot 68): one use of FactionMissionBonus (85D30F59) of today.
	character.dailyBonuses.uses[0x85D30F59U] = {1, 4'000'000'000'000'000'000ULL};
	bytes = rep::encode_game_event_state(character, rep::View::owner, clock);
	fragment = fragment_of(bytes, swc::net::kSlotGameEvent, swc::net::kStateGameEvent.index, whole);
	check(whole && RepClient::apply_daily_bonuses(fragment, own) && own.dailyBonusesUsed.size() == 1
	          && own.dailyBonusesUsed[0] == std::make_pair(0x85D30F59U, std::uint8_t{1}) && own.dailyBonusRevision == 1,
	      "the daily bonuses: one use of FactionMissionBonus (" + std::to_string(bytes.size()) + " bytes)");
	// The TerritoryInterface state of the Windsward board (slot 3): two always available projects.
	backend::world::Entity board;
	board.kind = backend::world::EntityKind::object;
	board.object.territorySlot = 3;
	board.object.territory = 6;
	board.object.projects = {0x2591C412U, 0x11112222U};
	bytes = rep::encode_territory_interface_state(board, rep::View::observer, clock);
	fragment = fragment_of(bytes, 3, swc::net::kStateTerritoryInterface, whole);
	check(whole, "the vendored reader takes the TerritoryInterface state (" + std::to_string(bytes.size()) + " bytes)");
	RepClient::OtherEntity other;
	decoder::Record record;
	record.fragments.push_back(fragment);
	RepClient::apply_other_fragments(record, other);
	check(other.hasBoardProjects && other.boardProjects.size() == 2 && other.boardProjects[0].key == 6
	          && other.boardProjects[0].project == 0x2591C412U && other.boardProjects[1].project == 0x11112222U && other.boardRevision == 1,
	      "the board: two active projects with the key 6 (AlwaysAvailable)");
	// The LandClaimManager state of the world entity: the claims of Windsward (6) and Everfall (4) with their projects.
	backend::world::Entity worldEntity;
	worldEntity.kind = backend::world::EntityKind::object;
	worldEntity.object.world = true;
	worldEntity.object.landClaims = {{4, {0x33334444U}}, {6, {0x2591C412U, 0x11112222U}}};
	bytes = rep::encode_land_claim_state(worldEntity, rep::View::observer, clock);
	fragment = fragment_of(bytes, 2, swc::net::kStateLandClaimManager, whole);
	check(whole, "the vendored reader takes the LandClaimManager state (" + std::to_string(bytes.size()) + " bytes)");
	RepClient::OtherEntity world;
	decoder::Record claimRecord;
	claimRecord.fragments.push_back(fragment);
	RepClient::apply_other_fragments(claimRecord, world);
	check(world.hasClaims && world.claimKeys == std::vector<std::uint16_t>{4, 6} && world.claimSummaries.size() == 2,
	      "the claims: the keys 4 and 6 with two summaries");
	check(world.claimSummaries.size() == 2 && world.claimSummaries[1].active.size() == 2 && world.claimSummaries[1].active[0].project == 0x2591C412U
	          && world.claimSummaries[1].active[0].type == 6 && world.claimSummaries[1].active[0].state == 0 && world.claimSummaries[1].level == 0
	          && world.claimSummaries[1].upgrades.empty(),
	      "the summary of Windsward: the AlwaysAvailable projects, Active, level 0, no upgrade");
	// The requests: 3819, 3816, and 2461 on the ObjectiveInteractor facet (G + B8D673AF8594A7CC); the readers of the backend take them.
	const std::uint64_t group = 0x4C554F5340000100ULL;
	const std::uint64_t target = group + 0xB8D673AF8594A7CCULL;
	const auto goal = rep::read_community_goal_selection(swc::net::community_goal_selection_body(group, 0x9D2C7B11U, 0x2591C412U));
	check(goal && goal->target == target && goal->entity == group && goal->mission == 0x9D2C7B11U && goal->destination == 0x2591C412U
	          && swc::net::community_goal_selection_body(group, 1, 0).size() == 24,
	      "3819: the backend reads the facet, the mission, and the project of the card (24 bytes)");
	const auto selection = rep::read_objective_selection(swc::net::objective_selection_body(group, 2));
	check(selection && selection->target == target && selection->entity == group && selection->index == 2
	          && swc::net::objective_selection_body(group, 0).size() == 17,
	      "3816: the backend reads the index 2 (17 bytes)");
	const auto completion = rep::read_objective_call(swc::net::objective_completion_body(group, 0x4C554F5340000042ULL), true);
	check(completion && completion->target == target && completion->entity == group && completion->value == 0x4C554F5340000042ULL
	          && swc::net::objective_completion_body(group, 1).size() == 24,
	      "2461: the backend reads the instance (24 bytes)");
}

void encounter_states(const backend::gamedata::QuestData& data)
{
	std::printf("encounter states (QG5)\n");
	backend::tests::ManualClock clock;
	namespace rep = backend::server::rep;
	backend::world::Entity root;
	root.kind = backend::world::EntityKind::encounter;
	root.encounter.managerSlot = 2;
	root.encounter.placeSlot = 3;
	root.encounter.objectivesSlot = 4;
	root.encounter.managerStatus = 2;
	root.encounter.objectiveInstance = 0x4C554F534000002AULL;
	root.encounter.objectiveCrc = 0x12345678U;
	root.encounter.tasks = {{0, 1, 2}, {1, 1, 2}, {2, 1, 1}, {3, 0, 1}, {4, 0, 0}};
	root.encounter.taskStarts = {{3, 1792600000123456789ULL}};
	root.encounter.stages = {{7, 2, {{1, 2}, {2, 3}}}, {8, 1, {{1, 2}}}};
	decoder::Record record;
	record.interest = 2;
	bool whole = false;
	const backend::server::hub::Bytes manager = rep::encode_encounter_manager_state(root, rep::View::observer, clock);
	record.fragments.push_back(fragment_of(manager, 2, swc::net::kStateEncounterManager, whole));
	check(whole, "the vendored reader takes the manager state 6786 (" + std::to_string(manager.size()) + " bytes)");
	for (const rep::SlotState& stage : rep::encode_encounter_stage_states(root, rep::View::observer, rep::PartsRequest{})) {
		record.fragments.push_back(fragment_of(stage.state, stage.slot, swc::net::kStateEncounterStage, whole));
		check(whole, "the vendored reader takes the stage state 2133 of slot " + std::to_string(stage.slot));
	}
	const backend::server::hub::Bytes objectives = rep::encode_encounter_objectives_state(root, rep::View::observer, clock);
	record.fragments.push_back(fragment_of(objectives, 4, swc::net::kStateObjectives.index, whole));
	check(whole, "the vendored reader takes the root Objectives (" + std::to_string(objectives.size()) + " bytes)");
	RepClient::OtherEntity entity;
	RepClient::apply_other_fragments(record, entity);
	check(entity.hasEncounterStatus && entity.encounterStatus == 2, "the manager status 2 (in progress)");
	const auto* slot7 = entity.encounter_stage(7);
	const auto* slot8 = entity.encounter_stage(8);
	check(entity.encounterStages.size() == 2 && slot7 && slot7->sequence == 2 && slot7->status == 3 && slot8 && slot8->status == 2,
	      "the last entry of each stage (slot 7: sequence 2 status 3; slot 8: status 2)");
	check(entity.hasRootObjectives && entity.rootActive.size() == 1 && entity.rootActive[0].instance == 0x4C554F534000002AULL
	          && entity.rootActive[0].poiObjective, "the root Objectives: one instance with isPoiObjective");
	check(entity.rootTasks.size() == 5 && entity.rootTasks[2].progress == 1 && entity.rootTasks[3].state == 1 && entity.rootTasks[4].index == 4,
	      "the root task elements in their index order with the progress and the state");
	check(entity.rootStarts.size() == 1 && entity.rootStarts[0].index == 3 && entity.rootStarts[0].startNs == 1792600000123456789ULL,
	      "the start of the timed leaf (field 0)");
	// An older or equal stage sequence changes nothing; a newer one replaces the entry.
	const std::uint32_t revision = entity.encounterRevision;
	root.encounter.stages = {{7, 2, {{2, 4}}}};
	decoder::Record same;
	for (const rep::SlotState& stage : rep::encode_encounter_stage_states(root, rep::View::observer, rep::PartsRequest{})) {
		same.fragments.push_back(fragment_of(stage.state, stage.slot, swc::net::kStateEncounterStage, whole));
	}
	RepClient::apply_other_fragments(same, entity);
	check(entity.encounter_stage(7)->status == 3 && entity.encounterRevision == revision, "an equal sequence of a stage is not applied");
	root.encounter.stages = {{7, 3, {{3, 4}}}};
	decoder::Record newer;
	for (const rep::SlotState& stage : rep::encode_encounter_stage_states(root, rep::View::observer, rep::PartsRequest{})) {
		newer.fragments.push_back(fragment_of(stage.state, stage.slot, swc::net::kStateEncounterStage, whole));
	}
	RepClient::apply_other_fragments(newer, entity);
	check(entity.encounter_stage(7)->sequence == 3 && entity.encounter_stage(7)->status == 4 && entity.encounterRevision > revision,
	      "a newer sequence replaces the entry (status 4)");
	// A TArray of the snapshot moves its elements with memcpy (ut567 2026-10-29: a std::map of the stages crashed the copy after a move).
	{
		alignas(RepClient::OtherEntity) unsigned char first[sizeof(RepClient::OtherEntity)];
		alignas(RepClient::OtherEntity) unsigned char moved[sizeof(RepClient::OtherEntity)];
		auto* original = new (first) RepClient::OtherEntity(entity);
		std::memcpy(moved, first, sizeof(RepClient::OtherEntity));
		(void)original;
		auto* relocated = std::launder(reinterpret_cast<RepClient::OtherEntity*>(moved));
		const RepClient::OtherEntity copy(*relocated);
		check(copy.encounterStages.size() == 2 && copy.encounter_stage(7)->status == 4 && copy.encounter_stage(8)->status == 2,
		      "a copy of an entity that a memcpy moved keeps the stages");
		relocated->~OtherEntity();
	}
	// The VariationData of a start object (field 1, u16).
	backend::world::Entity device;
	device.kind = backend::world::EntityKind::object;
	device.object.variationSlot = 1;
	device.object.variant = 733;
	decoder::Record variation;
	const backend::server::hub::Bytes variant = rep::encode_variation_state(device, rep::View::observer, clock);
	variation.fragments.push_back(fragment_of(variant, 1, swc::net::kStateVariationData, whole));
	check(whole, "the vendored reader takes the VariationData state 14");
	RepClient::OtherEntity object;
	RepClient::apply_other_fragments(variation, object);
	check(object.hasVariation && object.variation == 733, "the variant row 733 (the Obelisk device)");
	// The own link (field 5): no field before the first link, a list after it, and an empty list after the unlink.
	backend::world::Entity own;
	own.quests.loaded = true;
	own.quests.data = &data;
	RepClient::OwnEntity client;
	client.encounterLinks = {7};
	const backend::server::hub::Bytes before = rep::encode_objectives_state(own, rep::View::owner, clock);
	check(RepClient::read_objectives(fragment_of(before, swc::net::kSlotObjectives, swc::net::kStateObjectives.index, whole), client)
	          && client.encounterLinks == std::vector<std::uint64_t>{7}, "a state with no field 5 keeps the links");
	own.encounterLinked = true;
	own.encounterLinks = {0x4C554F534000002AULL};
	const backend::server::hub::Bytes linked = rep::encode_objectives_state(own, rep::View::owner, clock);
	check(RepClient::read_objectives(fragment_of(linked, swc::net::kSlotObjectives, swc::net::kStateObjectives.index, whole), client) && whole
	          && client.encounterLinks == std::vector<std::uint64_t>{0x4C554F534000002AULL}, "field 5 gives the root G");
	own.encounterLinks.clear();
	const backend::server::hub::Bytes unlinked = rep::encode_objectives_state(own, rep::View::owner, clock);
	check(RepClient::read_objectives(fragment_of(unlinked, swc::net::kSlotObjectives, swc::net::kStateObjectives.index, whole), client) && whole
	          && client.encounterLinks.empty(), "an empty field 5 clears the links");
	check(swc::net::group_of(swc::net::ActorId{0x2A, 0x00, 0x00, 0x40, 0x53, 0x4F, 0x55, 0x4C, 0x20, 0, 0, 0, 0x53, 0x4F, 0x55, 0x4C})
	          == 0x4C554F534000002AULL, "the G of a root is the group of its actor (the first 8 bytes, little-endian)");
}

int main(int argc, char** argv)
{
	try {
		if (argc < 2) {
			throw std::runtime_error("usage: swc-quest-tests QUEST-DATA.json");
		}
		backend::gamedata::QuestData data;
		check(data.load(argv[1]), std::string("the quest data of the backend copy loads: ") + argv[1]);
		objectives_state(data);
		encounter_states(data);
		faction_states();
		board_states();
		requests();
	} catch (const std::exception& error) {
		check(false, error.what());
	}
	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
