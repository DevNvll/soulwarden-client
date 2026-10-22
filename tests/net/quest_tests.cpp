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
#include <stdexcept>
#include <string>
#include <vector>

#include "Net/Codec/SwcRepClient.h"
#include "Net/Codec/SwcRepMessages.h"
#include "gamedata/quest_data.h"
#include "server/rep/conversation_messages.h"
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
}

} // namespace

int main(int argc, char** argv)
{
	try {
		if (argc < 2) {
			throw std::runtime_error("usage: swc-quest-tests QUEST-DATA.json");
		}
		backend::gamedata::QuestData data;
		check(data.load(argv[1]), std::string("the quest data of the backend copy loads: ") + argv[1]);
		objectives_state(data);
		requests();
	} catch (const std::exception& error) {
		check(false, error.what());
	}
	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
