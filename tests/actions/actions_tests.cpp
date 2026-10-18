// The tests of the attack graph of the client (tools/test-actions.sh; stage P2.7): the action order and the marker order
// with the converted data (SWC_CONTENT/player/actions.json), and the parity with the attack sequence of the backend
// (world/attack_sequence.cpp of the Soulwarden repository, read only) on the same input steps. The backend reads
// weapon-actions.json of the backend copy; the client reads the copy of that graph in actions.json.
//
// Usage: swc-actions-tests ACTIONS_JSON WEAPON_ACTIONS_JSON

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Combat/SwcAttackGraph.h"
#include "Combat/SwcContactFilter.h"
#include "gamedata/weapon_actions.h"
#include "world/attack_sequence.h"

// The ability ids of the backend file go through progression_id (progression_tables.cpp, not in this test): a stub. The basic
// attacks do not read it.
namespace soulwarden::backend::gamedata {
std::uint32_t progression_id(std::string_view text)
{
	std::uint32_t hash = 2166136261U;
	for (const char c : text) {
		hash = (hash ^ static_cast<unsigned char>(c)) * 16777619U;
	}
	return hash;
}
} // namespace soulwarden::backend::gamedata

namespace {

namespace backend = soulwarden::backend;
using swc::combat::Bits;
using swc::combat::Graph;
using Json = nlohmann::json;

int g_checks = 0;
int g_failed = 0;

void check(bool ok, const std::string& what)
{
	++g_checks;
	if (!ok) {
		++g_failed;
		std::printf("FAIL %s\n", what.c_str());
	}
}

/** One visit for the comparison: the fragment name and the start step. */
struct Seen {
	std::string name;
	unsigned start{0};
	bool operator==(const Seen& other) const { return name == other.name && start == other.start; }
};

std::string text_of(const std::vector<Seen>& seen)
{
	std::string out;
	for (const Seen& visit : seen) {
		out += visit.name + "@" + std::to_string(visit.start) + " ";
	}
	return out;
}

/** The client graph of a weapon of actions.json (the same fields that the engine loader reads, SwcActions.cpp). */
Graph graph_of(const Json& weapon)
{
	Graph graph;
	for (const Json& item : weapon["graph"]) {
		swc::combat::Fragment fragment;
		fragment.name = item["name"].get<std::string>();
		fragment.length = item["length"].get<float>();
		fragment.canTransition = item["canTransition"].is_null() ? -1.0F : item["canTransition"].get<float>();
		fragment.switchBuffer = item["switch"][0].get<float>();
		fragment.switchAt = item["switch"][1].get<float>();
		for (const Json& window : item["dodge"]) {
			fragment.dodge.push_back({window[0].get<float>(), window[1].is_null() ? -1.0F : window[1].get<float>(), window[2].get<bool>()});
		}
		graph.fragments.push_back(fragment);
	}
	std::size_t index = 0;
	for (const Json& item : weapon["graph"]) {
		for (const Json& row : item["transitions"]) {
			swc::combat::Transition transition;
			transition.buffered = row[0].get<std::string>() == "buffered";
			transition.start = row[1].get<float>();
			transition.end = row[2].is_null() ? -1.0F : row[2].get<float>();
			transition.input = swc::combat::input_of(row[3].get<std::string>());
			transition.to = graph.index_of(row[4].get<std::string>());
			graph.fragments[index].transitions.push_back(transition);
		}
		++index;
	}
	graph.start = graph.index_of(weapon["start"].get<std::string>());
	return graph;
}

/** The marks of a fragment of actions.json. */
std::vector<swc::combat::Mark> marks_of(const Json& fragment)
{
	std::vector<swc::combat::Mark> out;
	for (const Json& row : fragment["markers"]) {
		out.push_back({row[0].get<std::string>(), row[1].get<float>(), row[2].is_null() ? -1.0F : row[2].get<float>()});
	}
	return out;
}

/**
 * The attack bits of a script of the button: for each step 1 (the button goes down), -1 (it goes up), or 0. The hold is on
 * each step while the button is down, from the step after the press (protocol notes 4.15).
 */
std::vector<Bits> bits_of(const std::vector<int>& script)
{
	std::vector<Bits> out;
	bool down = false;
	for (const int event : script) {
		Bits bits;
		if (event == 2) {
			bits.dodge = true;
		} else if (event > 0 && !down) {
			bits.press = true;
			down = true;
		} else if (event < 0 && down) {
			bits.release = true;
			down = false;
		}
		bits.hold = down && !bits.press;
		out.push_back(bits);
	}
	return out;
}

/** A script: a press at each `at` step, held for `steps` steps; `length` steps in all. */
std::vector<int> clicks(std::initializer_list<std::pair<int, int>> presses, int length)
{
	std::vector<int> script(static_cast<std::size_t>(length), 0);
	for (const auto& [at, steps] : presses) {
		script[static_cast<std::size_t>(at)] = 1;
		if (at + steps < length) {
			script[static_cast<std::size_t>(at + steps)] = -1;
		}
	}
	return script;
}

std::vector<Seen> run_client(const Graph& graph, const std::vector<Bits>& steps, std::uint16_t first)
{
	swc::combat::Sequence sequence;
	sequence.set_graph(&graph);
	std::vector<Seen> seen;
	std::uint32_t last = 0;
	for (std::size_t index = 0; index < steps.size(); ++index) {
		sequence.step(static_cast<std::uint16_t>(first + index), steps[index]);
		if (sequence.take_dodge()) {
			seen.push_back({"dodge", static_cast<unsigned>(index)});
		}
		for (const swc::combat::Visit& visit : sequence.visits()) {
			if (visit.number > last) {
				seen.push_back({graph.fragments[static_cast<std::size_t>(visit.fragment)].name, static_cast<std::uint16_t>(visit.start - first)});
				last = visit.number;
			}
		}
	}
	return seen;
}

std::vector<Seen> run_backend(const backend::gamedata::WeaponActionSet& actions, const std::vector<Bits>& steps, std::uint16_t first)
{
	backend::world::AttackSequences sequences;
	std::vector<Seen> seen;
	std::uint32_t last = 0;
	const auto base = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
	for (std::size_t index = 0; index < steps.size(); ++index) {
		backend::world::AttackBits bits;
		bits.press = steps[index].press;
		bits.hold = steps[index].hold;
		bits.release = steps[index].release;
		bits.dodge = steps[index].dodge;
		bits.dodgeAllowed = steps[index].dodgeAllowed;
		const auto now = base + std::chrono::milliseconds(static_cast<long>(index * 1000 / 30));
		sequences.step("c", 1, 1, static_cast<std::uint16_t>(first + index), bits, "w", &actions, now);
		if (sequences.take_dodge("c")) {
			seen.push_back({"dodge", static_cast<unsigned>(index)});
		}
		const auto* state = sequences.state_of("c");
		if (state == nullptr) {
			continue;
		}
		for (const backend::world::AttackVisit& visit : state->visits) {
			if (visit.number > last) {
				seen.push_back({actions.fragments[visit.fragment].name, static_cast<std::uint16_t>(visit.start - first)});
				last = visit.number;
			}
		}
	}
	return seen;
}

void expect_order(const Graph& graph, const std::vector<int>& script, const std::vector<Seen>& wanted, const std::string& what)
{
	const std::vector<Seen> seen = run_client(graph, bits_of(script), 100);
	check(seen == wanted, what + ": " + text_of(seen) + "(wanted " + text_of(wanted) + ")");
}

/** The action order of the sword (1H_Melee, Sword) with the windows of its data (the rules of protocol notes 4.32). */
void sword_order_tests(const Graph& sword)
{
	// A click: the button up 3 steps after the press. Attack_Heavy_NotHeld 0 to 0.333 s gives the light swing at the release.
	expect_order(sword, clicks({{0, 3}}, 60), {{"Attack_Start_Charge", 0}, {"Attack_Primary", 3}}, "sword click");
	// The light chain: a press in the buffered window of each swing (Attack_Primary 0.333 to 0.533 s: steps 10 to 15 of the
	// swing) starts the next charge at the end of the window; its release before the end gives the next swing at once.
	expect_order(sword, clicks({{0, 3}, {16, 2}, {26, 2}}, 120),
		{{"Attack_Start_Charge", 0}, {"Attack_Primary", 3}, {"Attack_Start_Charge_C2", 18}, {"Attack_Primary_Strike2", 18},
			{"Attack_Start_Charge_C3", 30}, {"Attack_Primary_Strike3", 30}},
		"sword light chain");
	// A press before the buffered window is lost (the war hammer check of the notes, here 0.2 s into the swing).
	expect_order(sword, clicks({{0, 3}, {8, 2}}, 80), {{"Attack_Start_Charge", 0}, {"Attack_Primary", 3}}, "sword early press is lost");
	// A press after the window: the instant window (from 0.533 s) starts the next charge at the press.
	expect_order(sword, clicks({{0, 3}, {24, 2}}, 80),
		{{"Attack_Start_Charge", 0}, {"Attack_Primary", 3}, {"Attack_Start_Charge_C2", 24}, {"Attack_Primary_Strike2", 26}},
		"sword late press");
	// The partial heavy attack: the release at 0.433 s (Attack_Primary_Released 0.333 to 0.667 s).
	expect_order(sword, clicks({{0, 12}}, 60), {{"Attack_Start_Charge", 0}, {"Attack_Heavy", 12}}, "sword partial heavy");
	// The full charge: the hold at 0.667 s (step 19: clock 20/30).
	expect_order(sword, clicks({{0, 30}}, 60), {{"Attack_Start_Charge", 0}, {"Attack_Heavy_Charged", 19}}, "sword full charge");
	// A long hold repeats the charged swing (its instant window CanAttack_Heavy takes the hold).
	const std::vector<Seen> repeat = run_client(sword, bits_of(clicks({{0, 120}}, 150)), 100);
	int charged = 0;
	for (const Seen& visit : repeat) {
		charged += visit.name == "Attack_Heavy_Charged" ? 1 : 0;
	}
	check(charged >= 3, "sword long hold repeats the charged swing: " + text_of(repeat));
	// The end of a fragment: at rest after the length; the next press starts a new attack.
	expect_order(sword, clicks({{0, 3}, {70, 3}}, 120),
		{{"Attack_Start_Charge", 0}, {"Attack_Primary", 3}, {"Attack_Start_Charge", 70}, {"Attack_Primary", 73}}, "sword new attack after the end");
}

/** The marker order: each mark of a fragment comes one time, in the order of its time, for frames of different lengths. */
void marker_order_tests(const Json& actions)
{
	int fragments = 0;
	for (const auto& [key, fragment] : actions["fragments"].items()) {
		const std::vector<swc::combat::Mark> marks = marks_of(fragment);
		if (marks.empty()) {
			continue;
		}
		++fragments;
		for (const float frame : {1.0F / 30.0F, 1.0F / 60.0F, 1.0F / 144.0F, 0.1F}) {
			std::vector<const swc::combat::Mark*> seen;
			float last = -1.0F;
			for (float t = 0.0F; t < 4.0F; t += frame) {
				for (const swc::combat::Mark* mark : swc::combat::marks_between(marks, last, t)) {
					seen.push_back(mark);
				}
				last = t;
			}
			bool ordered = true;
			for (std::size_t index = 1; index < seen.size(); ++index) {
				ordered = ordered && seen[index - 1]->start <= seen[index]->start;
			}
			std::size_t wanted = 0;
			for (const swc::combat::Mark& mark : marks) {
				wanted += mark.start < 4.0F - frame ? 1 : 0;
			}
			check(ordered && seen.size() >= wanted && seen.size() <= marks.size(), "marks of " + key + " in order, each one time");
		}
	}
	check(fragments > 100, "fragments with marks: " + std::to_string(fragments));
	// The draw and the sheathe of the sword: the gear moves at its mark, in the window of the switch marks.
	const Json& draw = actions["fragments"]["player_sword.adb|Unsheathe_UpperBody|1H_Melee+Sword"];
	const std::vector<swc::combat::Mark> drawMarks = marks_of(draw);
	const auto first = swc::combat::marks_between(drawMarks, -1.0F, 1.0F);
	std::vector<std::string> names;
	for (const swc::combat::Mark* mark : first) {
		names.push_back(mark->name);
	}
	const auto at = [&names](const std::string& name) { return std::find(names.begin(), names.end(), name) - names.begin(); };
	check(at("CanUnsheathe_Buffer") < at("unsheathe_weapon") && at("unsheathe_weapon") < at("CanUnsheathe"),
		"sword draw marks: CanUnsheathe_Buffer, unsheathe_weapon, CanUnsheathe");
	check(swc::combat::mark_active(drawMarks, "CanUnsheathe", 0.5F) && !swc::combat::mark_active(drawMarks, "CanUnsheathe", 0.2F),
		"sword draw CanUnsheathe from 0.35 s");
	const Json& swing = actions["fragments"]["player_sword.adb|Attack_Primary|1H_Melee+Sword"];
	const std::vector<swc::combat::Mark> swingMarks = marks_of(swing);
	check(swc::combat::mark_active(swingMarks, "Attack_Translation_Sword_FirstAttack", 0.1F)
			|| swc::combat::mark_active(swingMarks, "Attack_Translation_FirstAttack", 0.1F),
		"sword swing: its translation mark at 0.1 s");
}

/** The weapon switch (protocol notes 4.32, live checks SW-L1 to SW-L3) with two graphs of the data. */
void switch_tests(const Graph& oldGraph, const Graph& newGraph, int pressStep, const std::string& what)
{
	const auto graphOf = [&](std::uint32_t slot) { return slot == swc::combat::kMainHand1 ? &oldGraph : &newGraph; };
	// SW-L1: at rest, the key of the other weapon, then a click 2 steps later: the swing of the new weapon at the key + 3.
	{
		swc::combat::Sequence sequence;
		swc::combat::WeaponSwitch weapons;
		weapons.set_held(swc::combat::kMainHand1);
		sequence.set_graph(&oldGraph);
		const std::vector<Bits> steps = bits_of(clicks({{2, 1}}, 20));
		std::optional<unsigned> switchedAt;
		for (std::size_t index = 0; index < steps.size(); ++index) {
			const auto input = static_cast<std::uint16_t>(500 + index);
			if (index == 0) {
				weapons.key(swc::combat::kMainHand1 + 1, input);
			}
			Bits bits = steps[index];
			if (weapons.step(input, bits, sequence, graphOf).switched) {
				switchedAt = static_cast<unsigned>(index);
			}
			sequence.step(input, bits);
		}
		check(switchedAt == 0U && sequence.graph() == &newGraph, what + " SW-L1: the switch at the key");
		std::vector<Seen> seen;
		for (const swc::combat::Visit& visit : sequence.visits()) {
			seen.push_back({newGraph.fragments[static_cast<std::size_t>(visit.fragment)].name, static_cast<std::uint16_t>(visit.start - 500)});
		}
		check(seen.size() >= 2 && seen[0] == Seen{"Attack_Start_Charge", 2} && seen[1] == Seen{"Attack_Primary", 3},
			what + " SW-L1: the swing at the key + 3: " + text_of(seen));
	}
	// SW-L2: the key 4 steps into a swing, a press in the buffered window of the swing: the switch at the switch mark of the
	// swing, then the swing of the new weapon at the switch + 3 from the kept press. (The hold of a kept press in an instant
	// window of the swing continues the chain, as on the server: the press step of each weapon is in its buffered window.)
	{
		swc::combat::Sequence sequence;
		swc::combat::WeaponSwitch weapons;
		weapons.set_held(swc::combat::kMainHand1);
		sequence.set_graph(&oldGraph);
		const int swing = oldGraph.index_of("Attack_Primary");
		const float switchAt = oldGraph.fragments[static_cast<std::size_t>(swing)].switchAt;
		const std::vector<Bits> steps = bits_of(clicks({{0, 3}, {pressStep, 2}}, 120));
		std::optional<unsigned> switchedAt;
		std::optional<unsigned> swingStart;
		for (std::size_t index = 0; index < steps.size(); ++index) {
			const auto input = static_cast<std::uint16_t>(65530 + index); // the input numbers wrap at 16 bits
			if (index == 7) {
				weapons.key(swc::combat::kMainHand1 + 1, input);
			}
			Bits bits = steps[index];
			if (weapons.step(input, bits, sequence, graphOf).switched) {
				switchedAt = static_cast<unsigned>(index);
			}
			sequence.step(input, bits);
			if (sequence.graph() == &newGraph && sequence.playing() != nullptr && !swingStart) {
				swingStart = static_cast<unsigned>(index);
			}
		}
		// The swing started at step 3: its clock reaches the mark at the step 3 + ceil(mark x 30) - 1.
		const unsigned wanted = 3 + static_cast<unsigned>(std::max(std::ceil(switchAt * 30.0F - 0.001F), 1.0F)) - 1;
		check(switchAt >= 0.0F && switchedAt == wanted, what + " SW-L2: the switch at the switch mark of the swing (step " +
			std::to_string(switchedAt.value_or(0)) + ", wanted " + std::to_string(wanted) + ")");
		check(swingStart == wanted + 2, what + " SW-L2: the charge of the new weapon at the switch + 2 (" + std::to_string(swingStart.value_or(0)) + ")");
	}
	// SW-L3: the key in a held charge (no switch mark; the full charge starts no window in the 20 steps): the key is lost, the
	// weapon stays.
	{
		swc::combat::Sequence sequence;
		swc::combat::WeaponSwitch weapons;
		weapons.set_held(swc::combat::kMainHand1);
		sequence.set_graph(&oldGraph);
		const std::vector<Bits> steps = bits_of(clicks({{0, 40}}, 60));
		bool lost = false;
		bool switched = false;
		for (std::size_t index = 0; index < steps.size(); ++index) {
			const auto input = static_cast<std::uint16_t>(10 + index);
			if (index == 2) {
				weapons.key(swc::combat::kMainHand1 + 1, input);
			}
			Bits bits = steps[index];
			const auto result = weapons.step(input, bits, sequence, graphOf);
			lost = lost || result.lost;
			switched = switched || result.switched.has_value();
			sequence.step(input, bits);
		}
		check(lost && !switched && weapons.held() == swc::combat::kMainHand1, what + " SW-L3: the key in a held charge is lost");
	}
}

/** The driver: the button events to the bits of the steps, the key bits of an entry, the snapshot. */
void driver_tests(const Graph& sword, const Graph& other)
{
	swc::combat::Driver driver;
	driver.set_weapons(&sword, &other, swc::combat::kMainHand1);
	std::vector<std::vector<unsigned>> entries;
	const auto step = [&](std::uint16_t input, std::vector<unsigned> bits = {}) {
		driver.step(input, static_cast<std::int64_t>(input) * 33'333'333, bits);
		entries.push_back(bits);
	};
	const auto has = [](const std::vector<unsigned>& bits, unsigned bit) { return std::find(bits.begin(), bits.end(), bit) != bits.end(); };
	// A click in one frame of the game: the press at one step, the release at the next.
	driver.button(true);
	driver.button(false);
	step(1);
	step(2);
	step(3);
	check(has(entries[0], 555) && has(entries[0], 203) && has(entries[0], 408) && !has(entries[0], 191), "driver: the press bits");
	check(has(entries[1], 567) && !has(entries[1], 191) && !has(entries[1], 555), "driver: the release at the next step");
	check(entries[2].empty(), "driver: no bit after the release");
	swc::combat::Driver::Snapshot snapshot = driver.snapshot();
	check(snapshot.visits.size() == 2 && sword.fragments[static_cast<std::size_t>(snapshot.visits[1].fragment)].name == "Attack_Primary",
		"driver: the click plays the light swing");
	// A hold: the hold bit from the step after the press.
	driver.button(true);
	step(100);
	step(101);
	step(102);
	check(has(entries[3], 555) && has(entries[4], 191) && has(entries[5], 191), "driver: the hold bits");
	driver.button(false);
	step(103);
	check(has(entries[6], 567), "driver: the release bit");
	// The weapon key of an entry: at rest the switch at its step.
	step(200, {355, 306});
	snapshot = driver.snapshot();
	check(snapshot.switches == 1 && snapshot.switchSlot == swc::combat::kMainHand1 + 1 && snapshot.switchInput == 200 && snapshot.graph == &other,
		"driver: the key 2 at rest switches at once");
	check(snapshot.presses == 2 && snapshot.releases == 2 && snapshot.holds >= 2, "driver: the counts");
}

/** The strikes of the client (actions.json) are the strikes of the backend: the damage key and the window of each one, and
 *  the shapes of each strike (stage P2.8: the contacts must come in the windows of the server). */
void strike_tests(const Json& actions, const backend::gamedata::WeaponActions& tables)
{
	int strikes = 0;
	int shapes = 0;
	for (const Json& weapon : actions["weapons"]) {
		if (!weapon.contains("graph")) {
			continue;
		}
		std::string tags;
		for (const Json& tag : weapon["tags"]) {
			tags += (tags.empty() ? "" : ",") + tag.get<std::string>();
		}
		const backend::gamedata::WeaponActionSet* server = tables.of_tags(tags);
		if (server == nullptr) {
			continue;
		}
		std::size_t index = 0;
		for (const Json& item : weapon["graph"]) {
			const auto& wanted = server->fragments[index++].strikes;
			const Json& got = item["strikes"];
			bool same = got.size() == wanted.size();
			for (std::size_t strike = 0; same && strike < wanted.size(); ++strike) {
				std::string key = got[strike]["key"].get<std::string>();
				std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				same = key == wanted[strike].key && std::fabs(got[strike]["start"].get<float>() - wanted[strike].start) < 1e-5F
					&& std::fabs(got[strike]["end"].get<float>() - wanted[strike].end) < 1e-5F
					&& got[strike]["shapes"].size() == wanted[strike].shapes.size();
				shapes += static_cast<int>(wanted[strike].shapes.size());
				++strikes;
			}
			check(same, "the strikes of " + tags + " " + item["name"].get<std::string>() + " are the strikes of the backend");
		}
	}
	check(strikes > 100, "strikes compared: " + std::to_string(strikes));
	std::printf("strikes: %d with %d shapes\n", strikes, shapes);
}

/** The stamina and the dodge (stage P2.5) with the values of the backend copy and the live checks of the protocol notes 4.38. */
void stamina_tests(const swc::combat::StaminaData& data)
{
	using swc::combat::DodgeVerdict;
	const auto near_value = [](float a, float b) { return std::fabs(a - b) < 0.05F; };
	// The steps of the client: 1/30 s each.
	const auto run = [&](swc::combat::Stamina& stamina, double from, double to) {
		for (double t = from; t <= to + 1e-9; t += 1.0 / 30.0) {
			stamina.advance(t);
		}
	};
	{
		// LS5: a dodge pays 45 at 0.1 s; no regeneration to CanTransition (0.5 s), then the delay of the exit (1 s); 33.33 in a second.
		swc::combat::Stamina stamina;
		stamina.set_data(data);
		stamina.refill(0.0);
		check(stamina.dodge(0.0) == DodgeVerdict::accepted, "LS5: a dodge at full stamina");
		run(stamina, 0.0, 0.0667);
		check(near_value(stamina.value(), 100.0F), "LS5: no payment before the mark 0.1 s");
		run(stamina, 0.1, 1.5);
		check(near_value(stamina.value(), 55.0F), "LS5: 55 after the payment, no regeneration to 1.5 s (" + std::to_string(stamina.value()) + ")");
		run(stamina, 1.5333, 2.5);
		check(near_value(stamina.value(), 55.0F + 33.33F), "LS5: 33.33 in the second after the delay (" + std::to_string(stamina.value()) + ")");
		run(stamina, 2.5333, 3.5);
		check(near_value(stamina.value(), 100.0F), "LS5: full again");
	}
	{
		// P5 (the live proof of 3.4f-B): four presses 0.73 s apart pay 45, 45, 10, the fourth is refused (exhausted); winded for
		// 2 s; the stamina is full about 4.4 s after the third payment.
		swc::combat::Stamina stamina;
		stamina.set_data(data);
		stamina.refill(0.0);
		std::vector<DodgeVerdict> verdicts;
		double t = 0.0;
		double emptyAt = -1.0;
		double fullAt = -1.0;
		for (int step = 0; step <= 30 * 8; ++step) {
			t = step / 30.0;
			stamina.advance(t);
			if (step == 0 || step == 22 || step == 44 || step == 66) {
				verdicts.push_back(stamina.dodge(t));
			}
			if (emptyAt < 0.0 && stamina.value() <= 0.0F) {
				emptyAt = t;
			}
			if (emptyAt >= 0.0 && fullAt < 0.0 && stamina.value() >= 100.0F) {
				fullAt = t;
			}
		}
		check(verdicts.size() == 4 && verdicts[0] == DodgeVerdict::accepted && verdicts[1] == DodgeVerdict::accepted
				&& verdicts[2] == DodgeVerdict::accepted && verdicts[3] == DodgeVerdict::exhausted,
			"P5: three dodges, the fourth refused (exhausted)");
		check(stamina.payments() == 3 && emptyAt > 0.0, "P5: three payments, then empty");
		check(fullAt > 0.0 && std::fabs((fullAt - emptyAt) - 4.4) < 0.15, "P5: full again 4.4 s after the third payment (" + std::to_string(fullAt - emptyAt) + " s)");
	}
	{
		// LS6: a dodge at 5 is accepted; the payment stops at 0 (no debt); at 0 winded for 2 s and exhausted until full.
		swc::combat::Stamina stamina;
		stamina.set_data(data);
		stamina.refill(0.0);
		stamina.pay(0.0, 95.0F);
		check(near_value(stamina.value(), 5.0F), "LS6: 5 stamina");
		check(stamina.dodge(0.0) == DodgeVerdict::accepted, "LS6: a dodge at 5 is accepted");
		run(stamina, 0.0, 0.2);
		check(stamina.value() == 0.0F && stamina.exhausted() && near_value(stamina.winded(0.2), 1.9F), "LS6: 0, exhausted, winded 2 s from the payment");
		check(stamina.dodge(0.6) == DodgeVerdict::exhausted, "LS6: an exhausted character gets no dodge");
		run(stamina, 0.2333, 5.0);
		check(!stamina.exhausted() && near_value(stamina.value(), 100.0F), "LS6: full, not exhausted");
	}
	{
		// The windows of a dodge that plays: a press at 0.1 s (no window, before CanTransition) is a duplicate; at 0.3 s (the
		// buffered window 0.267 to 0.5 s) the next dodge starts at 0.5 s; a second press in the window waits with it.
		swc::combat::Stamina stamina;
		stamina.set_data(data);
		stamina.refill(0.0);
		stamina.dodge(0.0);
		check(stamina.dodge(0.1) == DodgeVerdict::dodging, "a press at 0.1 s of a dodge is a duplicate");
		run(stamina, 0.0, 0.3);
		check(stamina.dodge(0.3) == DodgeVerdict::buffered && stamina.dodge(0.35) == DodgeVerdict::buffered, "a press at 0.3 s waits for 0.5 s");
		run(stamina, 0.3333, 0.4667);
		check(stamina.dodge_state()->number == 1, "the next dodge does not start before 0.5 s");
		run(stamina, 0.5, 0.7);
		check(stamina.dodge_state()->number == 2 && std::fabs(stamina.dodge_state()->start - 0.5) < 1e-6 && stamina.payments() == 2,
			"the next dodge starts at 0.5 s and pays at its mark");
		check(stamina.dodge(1.1) == DodgeVerdict::accepted && stamina.dodge_state()->number == 3, "a press in the instant window starts at once");
	}
	{
		// The equip load class (the encumbrance row of the backend copy: the maximum 500 tenths, 26 %, 46 %, 100 %) and the
		// armor of the exit check: the heavy chest of 110 tenths, with the heavy legs 173, with the head and the hands 267.
		using swc::combat::LoadClass;
		const auto load = [](float tenths) { return swc::combat::load_class_of(tenths, 500.0F, 26.0F, 46.0F, 100.0F); };
		check(load(0.0F) == LoadClass::light && load(129.0F) == LoadClass::light && load(130.0F) == LoadClass::medium
				&& load(229.0F) == LoadClass::medium && load(230.0F) == LoadClass::heavy && load(500.0F) == LoadClass::heavy
				&& load(501.0F) == LoadClass::overburdened,
			"the limits of the load classes");
		check(load(110.0F) == LoadClass::light && load(173.0F) == LoadClass::medium && load(267.0F) == LoadClass::heavy,
			"the armor of the exit check: light, medium, heavy");
	}
	{
		// The load class: the cost of the heavy dodge (50) and no dodge for an overburdened character.
		swc::combat::Stamina stamina;
		stamina.set_data(data);
		stamina.set_load(swc::combat::LoadClass::heavy);
		stamina.refill(0.0);
		stamina.dodge(0.0);
		run(stamina, 0.0, 0.2);
		check(near_value(stamina.value(), 50.0F), "a heavy dodge pays 50");
		stamina.set_load(swc::combat::LoadClass::overburdened);
		check(stamina.dodge(2.0) == DodgeVerdict::overburdened, "no dodge when overburdened");
	}
}

/** The driver with the stamina: the dodge bit for each press, also for a refused one (live check LS9). */
void driver_dodge_tests(const Graph& sword, const swc::combat::StaminaData& data)
{
	swc::combat::Driver driver;
	driver.set_stamina(data);
	driver.set_weapons(&sword, nullptr, swc::combat::kMainHand1);
	std::vector<unsigned> bits;
	int dodgeBits = 0;
	for (int step = 0; step < 90; ++step) {
		if (step == 0 || step == 22 || step == 44 || step == 66) {
			driver.dodge_press();
		}
		bits.clear();
		driver.step(static_cast<std::uint16_t>(1000 + step), step * 33'333'333LL, bits);
		dodgeBits += std::find(bits.begin(), bits.end(), swc::combat::kBitDodge) != bits.end() ? 1 : 0;
	}
	const swc::combat::Driver::Snapshot snapshot = driver.snapshot();
	check(dodgeBits == 4, "the driver: one dodge bit for each press (" + std::to_string(dodgeBits) + ")");
	check(snapshot.verdicts[static_cast<std::size_t>(swc::combat::DodgeVerdict::accepted)] == 3
			&& snapshot.verdicts[static_cast<std::size_t>(swc::combat::DodgeVerdict::exhausted)] == 1 && snapshot.exhausted,
		"the driver: three dodges and one refused press at rest with the sword (the sequence permits the dodge)");
}

/** One contact for each target for each strike (stage P2.8): two steps of one strike with two hit volumes of one creature
 *  give one contact; the next strike, the next visit, and a second creature give their own. */
void contact_filter_tests()
{
	swc::combat::ContactFilter filter;
	const std::uint64_t wolf = 0x4C554F5300000010ULL;
	const std::uint64_t boar = 0x4C554F5300000011ULL;
	check(filter.take(5, 0, wolf), "the first contact of a strike goes out");
	check(!filter.take(5, 0, wolf), "a second hit volume of the same creature in a later step of the strike: no second contact");
	check(filter.take(5, 0, boar), "a second creature in the same strike: its own contact");
	check(filter.take(5, 1, wolf), "the next strike of the visit: a new contact");
	check(filter.take(6, 0, wolf), "the next visit: a new contact");
	filter.reset();
	check(filter.take(5, 0, wolf), "after a reset (a new visit) the key is free again");
}

/** The parity with the backend: the same steps give the same fragments at the same steps, for each melee weapon. */
void parity_tests(const Json& actions, const backend::gamedata::WeaponActions& tables)
{
	std::mt19937 random(20261018U);
	int scripts = 0;
	for (const Json& weapon : actions["weapons"]) {
		if (!weapon.contains("graph")) {
			continue;
		}
		std::string tags;
		for (const Json& tag : weapon["tags"]) {
			tags += (tags.empty() ? "" : ",") + tag.get<std::string>();
		}
		const backend::gamedata::WeaponActionSet* server = tables.of_tags(tags);
		check(server != nullptr, "the backend has the graph of " + tags);
		if (server == nullptr) {
			continue;
		}
		const Graph client = graph_of(weapon);
		check(client.fragments.size() == server->fragments.size() && client.start >= 0
				&& client.fragments[static_cast<std::size_t>(client.start)].name == server->fragments[server->start].name,
			"the client graph of " + tags + " has the fragments of the backend");
		// Fixed scripts (the inputs of the live checks), then random clicks and holds.
		std::vector<std::vector<int>> all = {clicks({{0, 3}}, 90), clicks({{0, 3}, {16, 2}, {36, 2}, {60, 2}}, 200),
			clicks({{0, 12}}, 90), clicks({{0, 30}}, 120), clicks({{0, 120}}, 200), clicks({{0, 3}, {11, 3}, {22, 3}, {33, 3}, {44, 3}}, 150)};
		for (int index = 0; index < 300; ++index) {
			std::vector<int> script(400, 0);
			int at = static_cast<int>(random() % 20);
			while (at < 380) {
				const int hold = 1 + static_cast<int>(random() % (random() % 4 == 0 ? 60 : 12));
				script[static_cast<std::size_t>(at)] = 1;
				if (at + hold < 400) {
					script[static_cast<std::size_t>(at + hold)] = -1;
				}
				at += hold + 1 + static_cast<int>(random() % 30);
				if (random() % 3 == 0 && at < 400) {
					script[static_cast<std::size_t>(at)] = 2; // a dodge press
					at += 1 + static_cast<int>(random() % 20);
				}
			}
			all.push_back(script);
		}
		for (const std::vector<int>& script : all) {
			std::vector<Bits> steps = bits_of(script);
			// Some steps cannot start a dodge (the stamina system refuses it): the attack stays.
			for (Bits& bits : steps) {
				bits.dodgeAllowed = random() % 5 != 0;
			}
			const std::uint16_t first = static_cast<std::uint16_t>(random());
			const std::vector<Seen> a = run_client(client, steps, first);
			const std::vector<Seen> b = run_backend(*server, steps, first);
			++scripts;
			if (a != b) {
				check(false, "parity " + tags + ":\n  client  " + text_of(a) + "\n  backend " + text_of(b));
				break;
			}
			++g_checks;
		}
	}
	check(scripts > 1000, "parity scripts: " + std::to_string(scripts));
	std::printf("parity: %d scripts\n", scripts);
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 3) {
		std::fprintf(stderr, "usage: swc-actions-tests ACTIONS_JSON WEAPON_ACTIONS_JSON\n");
		return 2;
	}
	std::ifstream file(argv[1]);
	if (!file) {
		std::fprintf(stderr, "no %s (tools/convert-player.sh actions)\n", argv[1]);
		return 2;
	}
	const Json actions = Json::parse(file);
	backend::gamedata::WeaponActions tables;
	if (!tables.load(argv[2])) {
		std::fprintf(stderr, "the backend does not read %s: %s\n", argv[2], tables.error().c_str());
		return 2;
	}
	const auto weapon = [&actions](const std::string& tags) -> const Json* {
		for (const Json& item : actions["weapons"]) {
			std::string text;
			for (const Json& tag : item["tags"]) {
				text += (text.empty() ? "" : ",") + tag.get<std::string>();
			}
			if (text == tags && item.contains("graph")) {
				return &item;
			}
		}
		return nullptr;
	};
	const Json* sword = weapon("1H_Melee,Sword");
	const Json* greatsword = weapon("2H_Melee,GreatSword");
	const Json* hammer = weapon("2H_Melee,Warhammer");
	const Json* greataxe = weapon("2H_Melee,GreatAxe");
	if (sword == nullptr || greatsword == nullptr || hammer == nullptr || greataxe == nullptr) {
		std::fprintf(stderr, "actions.json has no graph of the sword, the greatsword, the war hammer, or the great axe\n");
		return 2;
	}
	// The stamina data of the backend copy (stamina-data.json, next to weapon-actions.json).
	swc::combat::StaminaData staminaData;
	{
		std::string folder = argv[2];
		folder = folder.substr(0, folder.find_last_of('/') + 1);
		std::ifstream staminaFile(folder + "stamina-data.json");
		const Json stamina = Json::parse(staminaFile);
		staminaData.max = stamina["component"]["max"].get<float>();
		staminaData.regenRate = stamina["component"]["regenRate"].get<float>();
		staminaData.maxWindedTime = stamina["component"]["maxWindedTime"].get<float>();
		staminaData.regenDelay = stamina["vitals"]["regenDelay"].get<float>();
		staminaData.regenDelayOnEmpty = stamina["vitals"]["regenDelayOnEmpty"].get<float>();
		staminaData.dodgeExitDelay = stamina["dodge"]["regenDelayOnExit"].get<float>();
		const char* classes[3] = {"light", "medium", "heavy"};
		for (int index = 0; index < 3; ++index) {
			const Json& row = stamina["dodge"]["classes"][classes[index]];
			const Json& marks = stamina["dodge"]["fragments"][row["fragment"].get<std::string>()];
			swc::combat::DodgeFragment fragment;
			fragment.name = row["fragment"].get<std::string>();
			fragment.pay = marks["pay"].get<float>();
			fragment.immuneEnd = marks["immune"][1].get<float>();
			fragment.canTransition = marks["canTransition"].get<float>();
			for (const Json& window : marks["cancel"]) {
				fragment.cancel.push_back({window[0].get<float>(), window[1].is_null() ? std::optional<float>() : std::optional<float>(window[1].get<float>()),
					window[2].get<bool>()});
			}
			staminaData.dodges[static_cast<std::size_t>(index)] = fragment;
			staminaData.dodgeCosts[static_cast<std::size_t>(index)] = row["cost"].get<float>();
		}
		staminaData.loaded = true;
	}
	const Graph swordGraph = graph_of(*sword);
	const Graph greatswordGraph = graph_of(*greatsword);
	const Graph hammerGraph = graph_of(*hammer);
	const Graph greataxeGraph = graph_of(*greataxe);
	sword_order_tests(swordGraph);
	marker_order_tests(actions);
	switch_tests(hammerGraph, greataxeGraph, 18, "war hammer to great axe");
	switch_tests(swordGraph, greatswordGraph, 12, "sword to greatsword");
	driver_tests(swordGraph, greatswordGraph);
	parity_tests(actions, tables);
	strike_tests(actions, tables);
	contact_filter_tests();
	stamina_tests(staminaData);
	driver_dodge_tests(swordGraph, staminaData);
	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed == 0 ? 0 : 1;
}
