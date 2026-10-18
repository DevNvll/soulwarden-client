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
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Combat/SwcAttackGraph.h"
#include "Combat/SwcContactFilter.h"
#include "Combat/SwcLife.h"
#include "gamedata/weapon_actions.h"
#include "world/attack_sequence.h"

// The ids of the backend file (abilities, nodes, perks) go through progression_id (progression_tables.cpp, not in this test: it
// is the CRC-32 of the text with A to Z in lower case, item_id_crc): the client function, which the ability tests compare with
// the ids of actions.json (the converter writes them with zlib).
namespace soulwarden::backend::gamedata {
std::uint32_t progression_id(std::string_view text)
{
	return swc::combat::progression_id(text);
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

/** @return The progression ids of a list of names of actions.json. */
std::vector<std::uint32_t> ids_of(const Json& names)
{
	std::vector<std::uint32_t> out;
	for (const Json& name : names) {
		out.push_back(swc::combat::progression_id(name.get<std::string>()));
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
		for (const Json& marker : item.value("markers", Json::array())) {
			fragment.markers.push_back({marker[0].get<std::string>(), marker[1].get<float>()});
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
			// Stage P3.5: the ability of a start of an ability, the terms of a transition inside an ability (row 6).
			if (transition.input == swc::combat::Input::ability && row.size() > 6 && row[6].is_string()) {
				transition.ability = swc::combat::progression_id(row[6].get<std::string>());
			} else if (transition.input == swc::combat::Input::terms && row.size() > 6 && row[6].is_array()) {
				for (const Json& term : row[6]) {
					std::vector<swc::combat::Literal> literals;
					bool known = true;
					for (const Json& value : term) {
						swc::combat::Literal literal;
						const bool count = value[1].is_number_unsigned();
						known = known && swc::combat::literal_of(value[0].get<std::string>(), count ? std::string() : value[1].get<std::string>(),
							count ? value[1].get<std::uint32_t>() : 0, value[2].get<bool>(), literal);
						literals.push_back(literal);
					}
					if (known) {
						transition.terms.push_back(std::move(literals));
					}
				}
			}
			graph.fragments[index].transitions.push_back(transition);
		}
		++index;
	}
	graph.start = graph.index_of(weapon["start"].get<std::string>());
	// Stage P3.5: the abilities of the weapon (the fragment of the action, the selections, the cooldown, the shots of the casts).
	for (const Json& item : weapon.value("abilities", Json::array())) {
		swc::combat::Ability ability;
		ability.name = item["ability"].get<std::string>();
		ability.id = item["crc"].get<std::uint32_t>();
		ability.fragment = graph.index_of(item["fragment"].get<std::string>());
		for (const Json& row : item["selections"]) {
			ability.selections.push_back({graph.index_of(row[0].get<std::string>()), ids_of(row[1]), ids_of(row[2]), ids_of(row[3]), ids_of(row[4])});
		}
		ability.cooldown = item["cooldown"].is_null() ? 0.0F : item["cooldown"].get<float>();
		ability.minCooldown = item["minCooldown"].is_null() ? 0.0F : item["minCooldown"].get<float>();
		for (const Json& row : item.value("shots", Json::array())) {
			ability.shots.push_back({row[0].get<std::string>(), row[3].get<std::uint32_t>(), ids_of(row[4]), ids_of(row[5])});
		}
		graph.abilities.push_back(ability);
	}
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
		bits.attackAllowed = steps[index].attackAllowed;
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

/**
 * The keys of the bar in the driver (stage P3.5): the bits of a press (protocol notes 4.22 "The keys"), the press that plays (the
 * fragment of the ability, its own cooldown with the time of the data), a press in the own cooldown (the bits go out, no start), the
 * correction from the cooldown state of the server (an entry sets the end; a reset keeps the own end), a press that the fragment
 * that plays does not take, an empty place, the collision of Charge, and the shot of Gravity Well at its mark.
 */
void driver_ability_tests(const Graph& greataxe, const Graph& hammer)
{
	using swc::combat::PressVerdict;
	using swc::combat::progression_id;
	const std::uint32_t charge = progression_id("Ability_Greataxe_Charge");
	const std::uint32_t maelstrom = progression_id("Ability_Greataxe_Maelstorm");
	const std::uint32_t well = progression_id("Ability_Greataxe_GravityWell");
	const std::uint32_t wrecking = progression_id("Ability_WarHammer_WreckingBall");
	check(charge == 0x737F440BU && maelstrom == 0xADE1DC59U && well == 0xA3EBD08CU && wrecking == 0xFD3C4928U,
		"the ability ids are the ids of the bar on the wire (the build sheet)");
	const auto ns = [](int input) { return static_cast<std::int64_t>(input) * 33'333'333; };
	const auto has = [](const std::vector<unsigned>& bits, unsigned bit) { return std::find(bits.begin(), bits.end(), bit) != bits.end(); };
	const auto name_of = [&greataxe](const swc::combat::Visit& visit) { return greataxe.fragments[static_cast<std::size_t>(visit.fragment)].name; };
	{
		swc::combat::Driver driver;
		driver.set_weapons(&greataxe, &hammer, swc::combat::kMainHand1);
		driver.set_bars({charge, maelstrom, well}, {wrecking, 0, 0});
		std::vector<std::vector<unsigned>> entries;
		std::vector<std::pair<int, std::uint32_t>> shots;
		const auto step = [&](int input) {
			std::vector<unsigned> bits;
			std::vector<swc::combat::Shot> made;
			driver.step(static_cast<std::uint16_t>(input), ns(input), bits, &made);
			entries.push_back(bits);
			for (const swc::combat::Shot& shot : made) {
				shots.emplace_back(input, shot.attack);
			}
		};
		// The bits of a press of Q held for 3 steps: press and virtual, the hold, the release.
		driver.ability_key(0, true);
		step(10);
		step(11);
		step(12);
		driver.ability_key(0, false);
		step(13);
		check(has(entries[0], 402) && has(entries[0], 479) && !has(entries[0], 39) && has(entries[1], 39) && has(entries[2], 39)
				&& has(entries[3], 211) && !has(entries[3], 39),
			"driver: the bits of Q (402 and 479, the hold 39, the release 211)");
		swc::combat::Driver::Snapshot snapshot = driver.snapshot();
		check(snapshot.lastPress && snapshot.lastPress->verdict == PressVerdict::played && snapshot.lastPress->ability == charge
				&& snapshot.lastPress->at == 10 && snapshot.abilityPresses == 1,
			"driver: Q at rest plays Charge at the press");
		check(snapshot.visits.size() == 1 && name_of(snapshot.visits[0]) == "Ability_GreatAxe_Engage" && snapshot.visits[0].start == 10
				&& snapshot.visits[0].cast == snapshot.lastPress->cast,
			"driver: the fragment of Charge with the cast");
		check(snapshot.cooldowns.count(charge) == 1 && snapshot.cooldowns.at(charge) == ns(10) + 20'000'000'000LL,
			"driver: the own cooldown of Charge, 20 s from the press (ability-data.json)");
		// The charge with no collision and no node: the uppercut at 0.967 s (Engage_Uppercut_NoHit), then the end.
		for (int input = 14; input < 200; ++input) {
			step(input);
		}
		snapshot = driver.snapshot();
		check(snapshot.visits.size() == 2 && name_of(snapshot.visits[1]) == "Ability_GreatAxe_Engage_Uppercut" && snapshot.visits[1].start == 10 + 28
				&& snapshot.visits[1].cast == snapshot.visits[0].cast && driver.snapshot().visits.back().ended,
			"driver: the uppercut of Charge at 0.967 s keeps the cast, then the rest");
		// A press in the own cooldown: the bits go out, the client does not play it.
		driver.ability_key(0, true);
		driver.ability_key(0, false);
		step(200);
		step(201);
		snapshot = driver.snapshot();
		check(has(entries.end()[-2], 402) && snapshot.lastPress->verdict == PressVerdict::cooldown && snapshot.visits.size() == 2,
			"driver: a press in the own cooldown sends its bits and plays nothing");
		// The cooldown state of the server: its end (here 2 s later: a reduction of the server) is the end of the client.
		const std::uint64_t serverNow = 5'000'000'000ULL;
		driver.set_server_cooldowns({{charge, serverNow + 2'000'000}}, serverNow, ns(201));
		driver.set_server_cooldowns({{charge, serverNow + 2'000'000}}, serverNow + 33'333, ns(202));
		snapshot = driver.snapshot();
		check(snapshot.cooldownCorrections == 1, "driver: an entry of the server applies one time");
		driver.ability_key(0, true);
		driver.ability_key(0, false);
		step(259);
		step(260);
		check(driver.snapshot().lastPress->verdict == PressVerdict::cooldown, "driver: a press before the end of the server: the own cooldown");
		driver.ability_key(0, true);
		driver.ability_key(0, false);
		step(262);
		step(263);
		snapshot = driver.snapshot();
		check(snapshot.lastPress->verdict == PressVerdict::played && snapshot.cooldowns.at(charge) == ns(262) + 20'000'000'000LL,
			"driver: after the end of the server Charge plays again, with a new own cooldown");
		// A reset of the server (the map has no entry): the client keeps its own cooldown (live check I8-05).
		driver.set_server_cooldowns({}, serverNow + 3'000'000, ns(290));
		driver.ability_key(0, true);
		driver.ability_key(0, false);
		step(300);
		step(301);
		check(driver.snapshot().lastPress->verdict == PressVerdict::cooldown, "driver: a reset of the server keeps the own cooldown");
		for (int input = 302; input < 1000; ++input) {
			step(input);
		}
		// Gravity Well at rest, then R one step later: the fragment of Gravity Well takes no ability before 0.533 s (refused, no
		// cooldown of Maelstrom); the shot at the mark CastGavityWellProjectile (15 steps after the press: protocol notes 4.27).
		shots.clear();
		driver.ability_key(2, true);
		driver.ability_key(2, false);
		step(1000);
		driver.ability_key(1, true);
		driver.ability_key(1, false);
		step(1001);
		step(1002);
		snapshot = driver.snapshot();
		check(snapshot.lastPress->verdict == PressVerdict::refused && snapshot.lastPress->ability == maelstrom && snapshot.cooldowns.count(maelstrom) == 0
				&& has(entries.end()[-2], 115) && has(entries.end()[-2], 571),
			"driver: R in the first steps of Gravity Well: no window (its bits go out, no cooldown)");
		for (int input = 1003; input < 1100; ++input) {
			step(input);
		}
		check(shots.size() == 1 && shots[0] == std::pair<int, std::uint32_t>(1015, 0x8E14366BU) && driver.snapshot().shots == 1,
			"driver: one shot of Gravity Well, 15 steps after the press, with the attack 8E14366B");
		// An empty place of the bar of the war hammer.
		step(1100);
		driver.ability_key(1, true);
		driver.ability_key(1, false);
		std::vector<unsigned> keys = {355, 306};
		driver.step(1101, ns(1101), keys);
		step(1102);
		step(1103);
		check(driver.snapshot().held == swc::combat::kMainHand1 + 1, "driver: the war hammer in the hand");
		driver.ability_key(1, true);
		driver.ability_key(1, false);
		step(1110);
		check(driver.snapshot().lastPress->verdict == PressVerdict::empty, "driver: a place with no ability plays nothing");
	}
	{
		// The collision of the charge (character_collided): the uppercut at the step of the collision (Engage_Uppercut_Detected,
		// 0.133 to 0.933 s; the client sees it, the server takes it at the match of the contact).
		swc::combat::Driver driver;
		driver.set_weapons(&greataxe, &hammer, swc::combat::kMainHand1);
		driver.set_bars({charge, maelstrom, well}, {wrecking, 0, 0});
		driver.ability_key(0, true);
		driver.ability_key(0, false);
		std::vector<unsigned> bits;
		for (int input = 10; input < 20; ++input) {
			driver.step(static_cast<std::uint16_t>(input), ns(input), bits);
		}
		driver.collided();
		driver.step(20, ns(20), bits);
		const swc::combat::Driver::Snapshot snapshot = driver.snapshot();
		check(snapshot.visits.size() == 2 && name_of(snapshot.visits[1]) == "Ability_GreatAxe_Engage_Uppercut" && snapshot.visits[1].start == 20
				&& snapshot.collisions == 1,
			"driver: a collision in the charge starts the uppercut at its step");
	}
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
				// Stage P3.5: the rows of the strike (the learned nodes of each row and its shape).
				const Json& rows = got[strike].value("rows", Json::array());
				same = same && rows.size() == wanted[strike].rows.size();
				for (std::size_t row = 0; same && row < rows.size(); ++row) {
					same = rows[row][0].get<std::string>() == wanted[strike].rows[row].row && rows[row][1].size() == wanted[strike].rows[row].needs.size()
						&& rows[row][2].size() == wanted[strike].rows[row].forbids.size() && rows[row][3].get<std::uint32_t>() == wanted[strike].rows[row].shape;
				}
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

/**
 * The life and the input locks (stage P2.10, Combat/SwcLife): the death locks the input and ends a timed use; the respawn
 * request shows the loading screen; the death pose holds while it shows and while the cells of the new place load; the
 * respawn fragment frees the input at its mark CanTransition (0.533 s of the fragment Respawn); a timed use locks the input; a
 * heal of a dead character with no travel respawns at once.
 */
void life_tests()
{
	using swc::combat::Life;
	const float respawnMark = 0.53333F;
	Life life;
	check(!life.locked() && !life.owns_body(), "the character lives: the input is free");
	check(life.set_timed_use(true) && life.locked() && life.owns_body(), "a timed use locks the input and owns the body");
	check(life.set(true, false) == Life::Event::death && life.state() == Life::State::dead, "the dead mark: the death");
	check(!life.timed_use(), "the death ends the timed use");
	check(life.locked(), "the dead character: the input is locked");
	check(!life.set_timed_use(true), "no timed use while dead");
	check(life.set(true, false) == Life::Event::none, "the dead mark again: no second death");
	life.advance(10.0F, respawnMark);
	check(life.state() == Life::State::dead && life.clock() > 4.0F, "the death holds (its clock runs, the pose holds at the end)");
	// The give-up: the request shows the loading screen; then the vitals state with the mark clear, then the end of the screen.
	check(life.set(true, true) == Life::Event::none && life.locked(), "the respawn request: the loading screen, still dead");
	check(life.set(false, true) == Life::Event::waiting && life.state() == Life::State::waiting, "alive again under the loading screen: wait");
	const float held = life.clock();
	life.advance(2.0F, respawnMark);
	check(life.clock() == held && life.locked(), "while waiting the clock holds and the input is locked");
	check(life.set(false, false) == Life::Event::respawn && life.state() == Life::State::respawning, "the screen ends: the respawn plays");
	check(life.locked(), "the respawn before its mark: the input is locked");
	check(life.advance(0.5F, respawnMark) == Life::Event::none && life.locked(), "0.5 s: before CanTransition");
	check(life.advance(0.05F, respawnMark) == Life::Event::free && !life.locked() && !life.owns_body(), "at CanTransition the input is free");
	// A heal of a dead character (no travel): the respawn at once.
	check(life.set(true, false) == Life::Event::death, "a second death");
	check(life.set(false, false) == Life::Event::respawn, "a heal with no loading screen: the respawn at once");
	check(life.set(true, false) == Life::Event::death, "a death in the respawn fragment: the death again");
	// A far travel of a character that lives: the loading screen locks the input, no fragment.
	Life traveler;
	check(traveler.set(false, true) == Life::Event::none && traveler.locked() && !traveler.owns_body(), "a loading screen of a travel locks the input");
	check(traveler.set(false, false) == Life::Event::none && !traveler.locked(), "its end frees it");
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
				// Stage P3.3: an overencumbered character starts no attack from rest (AttackBits::attackAllowed).
				bits.attackAllowed = random() % 7 != 0;
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

/** The ability data of the client graph and of the backend graph (stage P3.5): the ids, the fragments, the transitions. */
void ability_data_tests(const Json& actions, const backend::gamedata::WeaponActions& tables)
{
	int abilities = 0;
	for (const Json& weapon : actions["weapons"]) {
		if (!weapon.contains("graph") || weapon.value("abilities", Json::array()).empty()) {
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
		const Graph client = graph_of(weapon);
		check(client.abilities.size() == server->abilities.size(), tags + ": the abilities of the backend");
		for (std::size_t place = 0; place < client.abilities.size() && place < server->abilities.size(); ++place) {
			// One ability can have two actions (the throw of the hatchet and its upper body variant): each one at its place; a press
			// takes the first one of the id, on the two sides (WeaponActionSet::ability, Graph::ability).
			const swc::combat::Ability& ability = client.abilities[place];
			++abilities;
			const backend::gamedata::AbilityAction* action = &server->abilities[place];
			check(ability.id == swc::combat::progression_id(ability.name) && action != nullptr && action->ability == ability.name,
				tags + ": the id of " + ability.name + " (the CRC-32 of the converter) is the id of the backend");
			if (action == nullptr) {
				continue;
			}
			check(ability.fragment == static_cast<int>(action->fragment) && ability.selections.size() == action->selections.size(),
				tags + ": the fragment and the selections of " + ability.name);
			check(ability.cooldown > 0.0F, tags + ": the cooldown of " + ability.name);
		}
		for (std::size_t index = 0; index < client.fragments.size(); ++index) {
			const swc::combat::Fragment& mine = client.fragments[index];
			const backend::gamedata::AttackFragment& theirs = server->fragments[index];
			bool same = mine.transitions.size() == theirs.transitions.size();
			for (std::size_t row = 0; same && row < mine.transitions.size(); ++row) {
				const swc::combat::Transition& a = mine.transitions[row];
				const backend::gamedata::AttackTransition& b = theirs.transitions[row];
				same = a.ability == b.abilityId && a.terms.size() == b.terms.size() && (a.to < 0 || static_cast<std::uint32_t>(a.to) == b.to);
				for (std::size_t term = 0; same && term < a.terms.size(); ++term) {
					same = a.terms[term].size() == b.terms[term].size();
					for (std::size_t literal = 0; same && literal < a.terms[term].size(); ++literal) {
						const swc::combat::Literal& x = a.terms[term][literal];
						const backend::gamedata::ConditionLiteral& y = b.terms[term][literal];
						same = static_cast<int>(x.kind) == static_cast<int>(y.kind) && x.id == y.id && x.count == y.count && x.negated == y.negated
							&& (x.kind != swc::combat::Literal::Kind::playing || x.text == y.text);
					}
				}
			}
			check(same, tags + ": the transitions of " + mine.name + " (the abilities, the terms) are the transitions of the backend");
		}
	}
	check(abilities >= 12, "abilities of the melee weapons: " + std::to_string(abilities));
	// The Gravity Well of the great axe: its cooldown (ability-data.json: 30 s), its shot at the mark (0.533 s) with the attack
	// "GravityWell" (protocol notes 4.27: 8E14366B), and the shot of the upgrade with Unyielding.
	for (const Json& weapon : actions["weapons"]) {
		if (!weapon.contains("graph") || weapon["tags"].dump() != R"(["2H_Melee","GreatAxe"])") {
			continue;
		}
		const Graph graph = graph_of(weapon);
		const swc::combat::Ability* well = graph.ability(0xA3EBD08CU);
		check(well != nullptr && well->cooldown == 30.0F && well->shots.size() == 2, "Gravity Well: the cooldown 30 s and two shots");
		if (well != nullptr && !well->shots.empty()) {
			swc::combat::Facts facts;
			const swc::combat::Ability::Shot* shot = well->shot_of(facts);
			check(shot != nullptr && shot->attack == 0x8E14366BU && shot->mark == "CastGavityWellProjectile", "Gravity Well: the shot attack 8E14366B");
			check(std::fabs(graph.fragments[static_cast<std::size_t>(well->fragment)].marker("CastGavityWellProjectile") - 0.533333F) < 0.001F,
				"Gravity Well: the mark of the throw at 0.533 s");
			facts.nodes.insert(swc::combat::progression_id("Upgrade_Greataxe_GravityWell_ArmorInWell"));
			check(well->shot_of(facts) == &well->shots[1], "Gravity Well with Unyielding: the shot of the upgrade spell");
		}
		const swc::combat::Ability* maelstrom = graph.ability(0xADE1DC59U);
		swc::combat::Facts reprieve;
		reprieve.nodes.insert(swc::combat::progression_id("Upgrade_Greataxe_Maelstorm_SpinAttack"));
		check(maelstrom != nullptr && graph.fragments[static_cast<std::size_t>(graph.fragment_of(*maelstrom, {}))].name == "Ability_GreatAxe_Maelstrom"
				&& graph.fragments[static_cast<std::size_t>(graph.fragment_of(*maelstrom, reprieve))].name == "Ability_GreatAxe_Maelstrom_Upgrade",
			"Maelstrom: the fragment of No Reprieve (the selection of Upgrade_Greataxe_Maelstorm_SpinAttack)");
	}
}

/** One step of an ability script: the attack button (1 down, -1 up), the block press, and a press of an ability. */
struct AbilityStep {
	int button{0};
	bool block{false};
	std::uint32_t ability{0};
};

/** The verdict of each press and the visits (the fragment and the step of its start), from the steps of a script. */
struct AbilityRun {
	std::vector<Seen> visits;
	std::vector<std::string> presses;
};

std::string verdict_of(int verdict, unsigned delay)
{
	return verdict == 0 ? "now" : verdict == 1 ? "later+" + std::to_string(delay) : "refused";
}

/** The fragment of an ability for the learned nodes and the perks by the rule of the backend (Abilities::press_entity). */
int backend_fragment(const backend::gamedata::AbilityAction& action, const swc::combat::Facts& facts)
{
	const auto all = [&](const std::vector<std::string>& names, const std::set<std::uint32_t>& set) {
		return std::all_of(names.begin(), names.end(), [&](const std::string& name) { return set.count(swc::combat::progression_id(name)) > 0; });
	};
	const auto none = [&](const std::vector<std::string>& names, const std::set<std::uint32_t>& set) {
		return std::none_of(names.begin(), names.end(), [&](const std::string& name) { return set.count(swc::combat::progression_id(name)) > 0; });
	};
	std::uint32_t fragment = action.fragment;
	for (const backend::gamedata::AbilityAction::Selection& selection : action.selections) {
		if (all(selection.needs, facts.nodes) && none(selection.forbids, facts.nodes) && all(selection.perks, facts.perks) && none(selection.perkForbids, facts.perks)) {
			fragment = selection.fragment;
		}
	}
	return static_cast<int>(fragment);
}

AbilityRun run_client_abilities(const Graph& graph, const std::vector<AbilityStep>& script, const swc::combat::Facts& facts, std::uint16_t first)
{
	swc::combat::Sequence sequence;
	sequence.set_graph(&graph);
	AbilityRun run;
	std::uint32_t last = 0;
	std::uint64_t casts = 0;
	bool down = false;
	for (std::size_t index = 0; index < script.size(); ++index) {
		const auto input = static_cast<std::uint16_t>(first + index);
		Bits bits;
		bits.press = script[index].button > 0 && !down;
		bits.release = script[index].button < 0 && down;
		down = bits.press || (down && !bits.release);
		bits.hold = down && !bits.press;
		bits.block = script[index].block;
		sequence.step(input, bits, facts);
		if (script[index].ability != 0) {
			const swc::combat::Ability* ability = graph.ability(script[index].ability);
			const swc::combat::AbilityStart start = sequence.start_ability(input, ability->id, graph.fragment_of(*ability, facts), ++casts);
			run.presses.push_back(verdict_of(start.verdict == swc::combat::AbilityStart::Verdict::now ? 0 : start.verdict == swc::combat::AbilityStart::Verdict::later ? 1 : 2,
				start.delay));
		}
		for (const swc::combat::Visit& visit : sequence.visits()) {
			if (visit.number > last) {
				run.visits.push_back({graph.fragments[static_cast<std::size_t>(visit.fragment)].name, static_cast<std::uint16_t>(visit.start - first)});
				last = visit.number;
			}
		}
	}
	return run;
}

AbilityRun run_backend_abilities(const backend::gamedata::WeaponActionSet& actions, const std::vector<AbilityStep>& script, const swc::combat::Facts& facts,
	std::uint16_t first)
{
	backend::world::AttackSequences sequences;
	backend::world::AttackFacts known;
	known.learned = [&facts](std::uint32_t node) { return facts.nodes.count(node) > 0; };
	known.perk = [&facts](std::uint32_t perk) { return facts.perks.count(perk) > 0; };
	AbilityRun run;
	std::uint32_t last = 0;
	std::uint64_t casts = 0;
	bool down = false;
	const auto base = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
	for (std::size_t index = 0; index < script.size(); ++index) {
		const auto input = static_cast<std::uint16_t>(first + index);
		backend::world::AttackBits bits;
		bits.press = script[index].button > 0 && !down;
		bits.release = script[index].button < 0 && down;
		down = bits.press || (down && !bits.release);
		bits.hold = down && !bits.press;
		bits.block = script[index].block;
		const auto now = base + std::chrono::milliseconds(static_cast<long>(index * 1000 / 30));
		sequences.step("c", 1, 1, input, bits, "w", &actions, now, known);
		if (script[index].ability != 0) {
			const backend::gamedata::AbilityAction* action = actions.ability(script[index].ability);
			const backend::world::AbilityStart start = sequences.start_ability("c", 1, 1, input, "w", &actions, action->abilityId,
				static_cast<std::uint32_t>(backend_fragment(*action, facts)), ++casts, now);
			run.presses.push_back(verdict_of(static_cast<int>(start.verdict), start.delaySteps));
		}
		if (const auto* state = sequences.state_of("c"); state != nullptr) {
			for (const backend::world::AttackVisit& visit : state->visits) {
				if (visit.number > last) {
					run.visits.push_back({actions.fragments[visit.fragment].name, static_cast<std::uint16_t>(visit.start - first)});
					last = visit.number;
				}
			}
		}
	}
	return run;
}

std::string text_of(const std::vector<std::string>& items)
{
	std::string out;
	for (const std::string& item : items) {
		out += item + " ";
	}
	return out;
}

/**
 * The parity of the presses of the abilities with the backend (stage P3.5; AttackSequences::start_ability, the step loop with the
 * pending start, the transitions inside an ability with their terms): the same scripts of clicks, block presses, and presses of
 * the abilities of the weapon, with random learned nodes, give the same verdicts and the same fragments at the same steps.
 */
void ability_parity_tests(const Json& actions, const backend::gamedata::WeaponActions& tables)
{
	std::mt19937 random(20261018U + 35U);
	int scripts = 0;
	int played = 0;
	int later = 0;
	int refused = 0;
	for (const Json& weapon : actions["weapons"]) {
		if (!weapon.contains("graph") || weapon.value("abilities", Json::array()).empty()) {
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
		const Graph client = graph_of(weapon);
		// The nodes of the conditions of the weapon (the terms, the selections): each script learns a random part of them.
		std::vector<std::uint32_t> nodes;
		for (const swc::combat::Fragment& fragment : client.fragments) {
			for (const swc::combat::Transition& transition : fragment.transitions) {
				for (const auto& term : transition.terms) {
					for (const swc::combat::Literal& literal : term) {
						if (literal.kind == swc::combat::Literal::Kind::node) {
							nodes.push_back(literal.id);
						}
					}
				}
			}
		}
		for (const swc::combat::Ability& ability : client.abilities) {
			for (const auto& selection : ability.selections) {
				nodes.insert(nodes.end(), selection.needs.begin(), selection.needs.end());
			}
		}
		for (int index = 0; index < 200; ++index) {
			swc::combat::Facts facts;
			for (const std::uint32_t node : nodes) {
				if (random() % 2 == 0) {
					facts.nodes.insert(node);
				}
			}
			std::vector<AbilityStep> script(360);
			for (std::size_t at = static_cast<std::size_t>(random() % 10); at < script.size();) {
				const unsigned kind = random() % 10;
				if (kind < 5) {
					script[at].ability = client.abilities[random() % client.abilities.size()].id;
				} else if (kind < 8) {
					script[at].button = 1;
					const std::size_t up = at + 1 + random() % (random() % 4 == 0 ? 40 : 8);
					if (up < script.size()) {
						script[up].button = -1;
					}
				} else {
					script[at].block = true;
				}
				at += 1 + random() % 25;
			}
			const std::uint16_t first = static_cast<std::uint16_t>(random());
			const AbilityRun a = run_client_abilities(client, script, facts, first);
			const AbilityRun b = run_backend_abilities(*server, script, facts, first);
			++scripts;
			for (const std::string& verdict : a.presses) {
				played += verdict == "now" ? 1 : 0;
				later += verdict.rfind("later", 0) == 0 ? 1 : 0;
				refused += verdict == "refused" ? 1 : 0;
			}
			if (a.visits != b.visits || a.presses != b.presses) {
				check(false, "ability parity " + tags + ":\n  client  " + text_of(a.visits) + "\n  backend " + text_of(b.visits) + "\n  client  "
					+ text_of(a.presses) + "\n  backend " + text_of(b.presses));
				break;
			}
			++g_checks;
		}
	}
	check(scripts >= 1000 && played > 0 && later > 0 && refused > 0, "ability parity scripts: " + std::to_string(scripts));
	std::printf("ability parity: %d scripts, presses: %d now, %d later, %d refused\n", scripts, played, later, refused);
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
	driver_ability_tests(greataxeGraph, hammerGraph);
	parity_tests(actions, tables);
	ability_data_tests(actions, tables);
	ability_parity_tests(actions, tables);
	strike_tests(actions, tables);
	contact_filter_tests();
	life_tests();
	stamina_tests(staminaData);
	driver_dodge_tests(swordGraph, staminaData);
	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed == 0 ? 0 : 1;
}
