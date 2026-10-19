// The tests of the movement rules of the client (tools/test-movement.sh; stage P3.3): the speed of each case against the motion
// lists of the data (SWC_CONTENT/player/movement.json) and the values of the research (feel-values.md R2.2, p33-movement.md),
// the posture keys, autorun, the limits of the load and of the statuses, and the position check of the backend
// (world/position_report.cpp, read only) with the reports of a simulated client at the speeds of the rules.
//
// Usage: swc-movement-tests MOVEMENT_JSON

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Character/SwcMoveRules.h"
#include "world/position_report.h"

namespace {

namespace move = swc::move;
namespace backend = soulwarden::backend;
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

bool near(float a, float b, float tolerance = 1e-4F)
{
	return std::fabs(a - b) <= tolerance;
}

move::Condition condition_of(const Json& value)
{
	move::Condition out;
	out.type = value[0].get<std::string>();
	for (const auto& [key, text] : value[1].items()) {
		out.attrs[key] = text.get<std::string>();
	}
	for (const Json& child : value[2]) {
		out.children.push_back(condition_of(child));
	}
	return out;
}

move::Motion motion_entry(const Json& value)
{
	move::Motion out;
	out.name = value.value("name", std::string());
	for (int axis = 0; axis < 3; ++axis) {
		out.speed[axis] = value["speed"][axis].get<float>();
		out.accel[axis] = value["accel"][axis].get<float>();
		out.decel[axis] = value["decel"][axis].get<float>();
	}
	out.backwards = value.value("backwards", 1.0F);
	return out;
}

/** The data of movement.json (the same fields that the engine loader reads, Character/SwcCharacterMovement.cpp). */
move::Data data_of(const Json& doc)
{
	move::Data data;
	for (const auto& [action, list] : doc["motions"].items()) {
		move::MotionList motions;
		if (!list["default"].is_null()) {
			motions.fallback = motion_entry(list["default"]);
		}
		for (const Json& item : list["conditionals"]) {
			motions.conditionals.push_back(motion_entry(item));
		}
		data.motions[action] = motions;
	}
	for (const auto& [action, list] : doc["facings"].items()) {
		move::FacingList facings;
		facings.fallback = list["default"].get<float>();
		for (const Json& item : list["conditionals"]) {
			facings.conditionals.emplace_back(item[0].get<std::string>(), item[1].get<float>());
		}
		data.facings[action] = facings;
	}
	for (const auto& [name, value] : doc["conditions"].items()) {
		data.conditions[name] = condition_of(value);
	}
	for (const auto& [index, row] : doc["statuses"].items()) {
		move::StatusRow status;
		status.id = row["id"].get<std::string>();
		status.noNav = row["noNav"].get<bool>();
		status.noRun = row["noRun"].get<bool>();
		status.noSprint = row["noSprint"].get<bool>();
		status.noDodge = row["noDodge"].get<bool>();
		status.stunned = row["stunned"].get<bool>();
		status.silenced = row["silenced"].get<bool>();
		status.moveSpeedMod = row["moveSpeedMod"].get<float>();
		status.sprintSpeedMod = row["sprintSpeedMod"].get<float>();
		status.categories = row["categories"].get<std::string>();
		data.statuses[static_cast<std::uint16_t>(std::stoi(index))] = status;
	}
	return data;
}

/** The entry of a motion list of the data by its name (the expected values of a case come from the data itself). */
const move::Motion* entry_named(const move::Data& data, const std::string& action, const std::string& name)
{
	const move::MotionList& list = data.motions.at(action);
	if (name.empty()) {
		return &list.fallback;
	}
	for (const move::Motion& motion : list.conditionals) {
		if (motion.name == name) {
			return &motion;
		}
	}
	return nullptr;
}

/** The facts of a character: its posture, its weapon tags (empty: unarmed), its inputs, and its limits. */
move::Facts facts_of(move::Posture posture, std::vector<std::string> weapon = {}, std::vector<std::string> inputs = {},
	const move::Limits& limits = {})
{
	move::Facts facts;
	facts.tags.insert(move::posture_tag(posture));
	if (weapon.empty()) {
		facts.tags.insert("Unarmed");
	}
	for (const std::string& tag : weapon) {
		facts.tags.insert(tag);
	}
	for (const std::string& input : inputs) {
		facts.inputs.insert(input);
	}
	limits.apply(facts);
	return facts;
}

/**
 * One case: the action, the facts, the name of the Conditional that the rules must choose (empty: the Default), and the
 * speed (forward, m/s) and the acceleration (forward, m/s2) of the research for it.
 */
void expect_motion(const move::Data& data, const std::string& action, const move::Facts& facts, const std::string& name, float speed,
	float accel, const std::string& what)
{
	const move::Motion* chosen = move::motion_of(data, action, facts);
	const move::Motion* wanted = entry_named(data, action, name);
	check(wanted != nullptr, what + ": the data has the conditional " + (name.empty() ? "Default" : name) + " of " + action);
	if (chosen == nullptr || wanted == nullptr) {
		return;
	}
	check(chosen == wanted, what + ": " + action + " chooses " + (chosen->name.empty() ? "Default" : chosen->name) + " (wanted "
		+ (name.empty() ? "Default" : name) + ")");
	check(near(chosen->speed[1], speed) && near(chosen->accel[1], accel), what + ": " + action + " " + std::to_string(chosen->speed[1])
		+ " m/s, accel " + std::to_string(chosen->accel[1]) + " (the research: " + std::to_string(speed) + ", " + std::to_string(accel) + ")");
}

void speed_tests(const move::Data& data)
{
	using move::Posture;
	const std::vector<std::string> axe = {"2H_Melee", "GreatAxe"};
	// The stand (research R2.2): the sprint 5.5 m/s (unarmed 20 m/s2; Stand+Armed: the start 10), the run 3.25, the walk 1.65.
	expect_motion(data, "Sprint", facts_of(Posture::stand), "ServerValidatedCanSprint", 5.5F, 20.0F, "unarmed sprint");
	expect_motion(data, "Sprint_Start", facts_of(Posture::stand), "ServerValidatedCanSprint", 5.5F, 20.0F, "unarmed sprint start");
	expect_motion(data, "Sprint_Start", facts_of(Posture::stand, axe), "Stand+Armed", 5.5F, 10.0F, "armed sprint start");
	expect_motion(data, "Sprint", facts_of(Posture::stand, axe), "Stand+Armed", 5.5F, 20.0F, "armed sprint");
	expect_motion(data, "Nav", facts_of(Posture::stand), "ServerValidatedCanRun", 3.25F, 35.0F, "unarmed run (the back key)");
	expect_motion(data, "Nav_Start", facts_of(Posture::stand), "NavStart", 3.25F, 12.0F, "unarmed run start");
	expect_motion(data, "Nav", facts_of(Posture::stand, axe), "Combat_Nav_Stand_Fast", 3.25F, 25.0F, "armed run, light load");
	expect_motion(data, "Nav", facts_of(Posture::stand, {}, {"walk"}), "Stand_Walk", 1.65F, 25.0F, "walk (Left Ctrl)");
	// The order of the data: the armed runs (Combat_Nav_Stand_*) come before Stand_Walk, and they do not read the walk key: with a
	// weapon drawn Left Ctrl gives no walk. The aim (the tags Aim and AimDownSights) walks.
	expect_motion(data, "Nav", facts_of(Posture::stand, axe, {"walk"}), "Combat_Nav_Stand_Fast", 3.25F, 25.0F, "armed run with Left Ctrl");
	move::Facts aimed = facts_of(Posture::stand, {"Bow"});
	aimed.tags.insert("Aim");
	aimed.tags.insert("AimDownSights");
	expect_motion(data, "Nav", aimed, "Stand_Walk", 1.65F, 25.0F, "the aim walks");
	// The crouch (p33-movement.md Q1): the crouch sprint 2.5 (start 10), the crouch run 1.25, the crouch walk 0.825.
	expect_motion(data, "Sprint", facts_of(Posture::crouch), "Crouch", 2.5F, 20.0F, "crouch sprint");
	expect_motion(data, "Sprint_Start", facts_of(Posture::crouch), "Crouch", 2.5F, 10.0F, "crouch sprint start");
	expect_motion(data, "Nav", facts_of(Posture::crouch), "Crouch_Unarmed", 1.25F, 25.0F, "unarmed crouch run");
	expect_motion(data, "Nav", facts_of(Posture::crouch, axe), "Combat_Nav_Crouch", 1.25F, 25.0F, "armed crouch run");
	// Crouch_Unarmed and Combat_Nav_Crouch come before Crouch_Walk: the crouch walk (0.825) is for a tool or a forced walk.
	expect_motion(data, "Nav", facts_of(Posture::crouch, {}, {"walk"}), "Crouch_Unarmed", 1.25F, 25.0F, "unarmed crouch with Left Ctrl");
	expect_motion(data, "Nav", facts_of(Posture::crouch, {"Tool"}, {"walk"}), "Crouch_Walk", 0.825F, 2.5F, "crouch walk with a tool");
	// The prone: the prone sprint 1.25, the prone run 0.375 (accel 1, decel 5), the prone walk 0.1875.
	expect_motion(data, "Sprint", facts_of(Posture::prone), "Prone", 1.25F, 20.0F, "prone sprint");
	expect_motion(data, "Nav", facts_of(Posture::prone), "Prone", 0.375F, 1.0F, "prone run");
	expect_motion(data, "Nav", facts_of(Posture::prone, {}, {"walk"}), "Prone_Walk", 0.1875F, 1.0F, "prone walk");
	check(near(move::motion_of(data, "Nav", facts_of(Posture::prone))->decel[1], 5.0F), "prone run: decel 5");
	// The load: an encumbered character walks (IfEncumbered: Stand_Walk); an overburdened one runs 3.25 (armed: _Slow).
	move::Limits heavy;
	heavy.bagWeight = 3010;
	expect_motion(data, "Nav", facts_of(Posture::stand, {}, {}, heavy), "Stand_Walk", 1.65F, 25.0F, "encumbered walk (300.1)");
	expect_motion(data, "Nav", facts_of(Posture::stand, axe, {}, heavy), "Stand_Walk", 1.65F, 25.0F, "encumbered armed walk");
	move::Limits light;
	light.bagWeight = 2010;
	expect_motion(data, "Nav", facts_of(Posture::stand, {}, {}, light), "ServerValidatedCanRun", 3.25F, 35.0F, "200.1 is no movement limit");
	move::Limits over;
	over.equipLoad = move::EquipLoad::overburdened;
	expect_motion(data, "Nav", facts_of(Posture::stand, axe, {}, over), "Combat_Nav_Stand_Slow", 3.25F, 25.0F, "overburdened armed run");
	expect_motion(data, "Nav", facts_of(Posture::stand, {}, {}, over), "ServerValidatedCanRun", 3.25F, 35.0F, "overburdened unarmed run");
	// The stop (Nav_Stop): 40 m/s2, 12 with a melee weapon (CombatNav+Melee).
	check(near(move::motion_of(data, "Nav_Stop", facts_of(Posture::stand))->decel[1], 40.0F), "the stop: decel 40");
	check(near(move::motion_of(data, "Nav_Stop", facts_of(Posture::stand, axe))->decel[1], 12.0F), "the stop with a melee weapon: decel 12");
	// The facings (research R2.3.2): Nav 0.30, aim or shoot 0.35, Sprint 1.0, Sprint_Start 0.33.
	check(near(move::facing_of(data, "Nav", facts_of(Posture::stand)), 0.30F), "Nav facing 0.30");
	move::Facts aim = facts_of(Posture::stand, {"Bow"});
	aim.tags.insert("AimDownSights");
	check(near(move::facing_of(data, "Nav", aim), 0.35F), "Nav facing with the aim 0.35");
	check(near(move::facing_of(data, "Sprint", facts_of(Posture::stand)), 1.0F), "Sprint facing 1.0");
	check(near(move::facing_of(data, "Sprint_Start", facts_of(Posture::stand)), 0.33F), "Sprint_Start facing 0.33");
	// Stage WF5, the wade (IfWaterDepth GreaterThanOrEqual Shallow and the setting Swimming): the first conditional of Nav, Nav_Start,
	// and Sprint; it wins over the armed and the walk entries. VeryShallow changes nothing.
	const auto in_water = [](move::Facts facts, std::uint8_t depth) {
		facts.waterDepth = depth;
		return facts;
	};
	expect_motion(data, "Sprint", in_water(facts_of(Posture::stand), 2), "Wade", 2.5F, 20.0F, "the sprint in a wade");
	expect_motion(data, "Sprint", in_water(facts_of(Posture::stand, axe), 2), "Wade", 2.5F, 20.0F, "the armed sprint in a wade");
	expect_motion(data, "Nav", in_water(facts_of(Posture::stand), 2), "Wade", 1.65F, 10.0F, "the run in a wade");
	expect_motion(data, "Nav_Start", in_water(facts_of(Posture::stand, {}, {"walk"}), 2), "Wade", 1.65F, 10.0F, "the run start in a wade");
	expect_motion(data, "Sprint", in_water(facts_of(Posture::crouch), 2), "Wade+Crouch", 1.75F, 20.0F, "the crouch sprint in a wade");
	expect_motion(data, "Sprint", in_water(facts_of(Posture::stand), 1), "ServerValidatedCanSprint", 5.5F, 20.0F, "VeryShallow: the sprint");
	expect_motion(data, "Sprint", in_water(facts_of(Posture::stand), 3), "Wade", 2.5F, 20.0F, "Deep: the wade conditional too");
	check(move::holds(data, "Wade", in_water(facts_of(Posture::stand), 2)) && !move::holds(data, "Wade", in_water(facts_of(Posture::stand), 1)),
		"the named condition Wade: Shallow or deeper");
	// A tag state name: "Wade+Crouch" is a named condition; "Crouch" is a tag state.
	check(move::holds(data, "Crouch", facts_of(Posture::crouch)) && !move::holds(data, "Crouch", facts_of(Posture::stand)),
		"the tag state Crouch");
}

void limit_tests(const move::Data& data)
{
	const auto row = [&data](const char* id) -> const move::StatusRow* {
		for (const auto& [index, status] : data.statuses) {
			if (status.id == id) {
				return &status;
			}
		}
		return nullptr;
	};
	// The bag (EncumbranceBaseMax 2000 tenths): above 200.0 no dodge and no ability; above 300.0 the movement condition.
	move::Limits limits;
	limits.bagWeight = 2000;
	check(!limits.encumbered() && limits.can_dodge() && limits.can_use_abilities(), "200.0: not encumbered");
	limits.bagWeight = 2001;
	check(limits.encumbered() && !limits.can_dodge() && !limits.can_use_abilities() && limits.can_sprint() && limits.can_jump(),
		"200.1: no dodge, no ability; the sprint and the jump stay (live: a sprint at 200.2)");
	limits.bagWeight = 3000;
	check(!limits.encumbered_movement() && limits.can_attack_from_rest(), "300.0: no movement limit");
	limits.bagWeight = 3001;
	check(limits.encumbered_movement() && !limits.can_sprint() && !limits.can_jump() && !limits.can_attack_from_rest(),
		"300.1: no sprint, no jump, no attack from rest (live: a walk at 300.2)");
	// The equip load: Overburdened gives no sprint, no dodge, no jump.
	move::Limits over;
	over.equipLoad = move::EquipLoad::overburdened;
	check(!over.can_sprint() && !over.can_dodge() && !over.can_jump() && over.can_run(), "overburdened: no sprint, dodge, jump");
	// A root of a creature (NoSprint, NoRun, NoNav), a stun, a slow, a root with NoDodge.
	const move::StatusRow* root = row("AI_Root_4sec");
	const move::StatusRow* tether = row("AI_PalatineArcher_StrandedTether_Root");
	const move::StatusRow* stun = row("AI_Stun_4sec");
	const move::StatusRow* snare = row("AI_Snare_6sec");
	check(root != nullptr && tether != nullptr && stun != nullptr && snare != nullptr, "the status rows of the creatures are in the data");
	if (root != nullptr && tether != nullptr && stun != nullptr && snare != nullptr) {
		move::Limits rooted;
		rooted.statuses = {root};
		check(!rooted.can_nav() && !rooted.can_run() && !rooted.can_sprint() && !rooted.can_jump() && rooted.can_dodge(),
			"AI_Root_4sec: no movement, no jump; the dodge stays (no NoDodge)");
		rooted.statuses = {tether};
		check(!rooted.can_dodge(), "the tether root has NoDodge");
		move::Limits stunned;
		stunned.statuses = {stun};
		check(stunned.stunned() && !stunned.can_nav() && !stunned.can_dodge() && !stunned.can_attack_from_rest() && !stunned.can_use_abilities(),
			"AI_Stun_4sec: the stun locks the movement and the actions");
		move::Limits slowed;
		slowed.statuses = {snare};
		slowed.sprintMultiplier = 0.5F;
		check(slowed.slowed() && near(slowed.jump_multiplier(), 0.85F) && near(slowed.speed_multiplier(), 0.5F)
				&& near(slowed.dodge_multiplier(), 0.5F) && slowed.can_nav(),
			"AI_Snare_6sec with the table 0.5: half speed, a jump of 0.85, a dodge of 0.5");
	}
	move::Limits haste;
	haste.sprintMultiplier = 1.15F;
	check(near(haste.speed_multiplier(), 1.15F) && near(haste.dodge_multiplier(), 1.0F), "the road haste 1.15: no longer dodge");
	haste.sprintMultiplier = 2.0F;
	check(near(haste.speed_multiplier(), 1.5F), "the multiplier stays in 0 to 1.5 (StatMultiplierData)");
	// The road haste of the speed table: the sprint 5.5 x 1.15 = 6.325 m/s (research R2.2).
	check(near(move::motion_of(data, "Sprint", facts_of(move::Posture::stand))->speed[1] * 1.15F, 6.325F, 1e-3F), "the road sprint 6.325 m/s");
}

void posture_tests()
{
	using move::Posture;
	move::PostureState posture;
	posture.toggle_crouch();
	check(posture.desired() == Posture::crouch && posture.pending() == Posture::crouch, "C: crouch desired");
	posture.arrive(Posture::crouch);
	posture.toggle_prone();
	check(posture.desired() == Posture::prone, "Z from crouch: prone");
	posture.arrive(Posture::prone);
	posture.toggle_crouch();
	check(posture.desired() == Posture::crouch, "C from prone: crouch");
	posture.arrive(Posture::crouch);
	posture.toggle_crouch();
	check(posture.desired() == Posture::stand, "C from crouch: stand");
	posture.arrive(Posture::stand);
	posture.toggle_prone();
	posture.arrive(Posture::prone);
	posture.toggle_prone();
	check(posture.desired() == Posture::stand, "Z from prone: stand");
	posture.force(Posture::stand);
	check(!posture.pending() && posture.posture() == Posture::stand, "a stand-up of an action (a jump, an attack)");
	const move::Transition idle = move::transition_of(Posture::stand, Posture::prone, false, false);
	check(idle.fragment == "Idle_PostureTransition+Prone+FromStand" && near(idle.canTransition, 0.4F), "the idle transition to prone");
	const move::Transition slide = move::transition_of(Posture::stand, Posture::crouch, true, true);
	check(slide.sprint && near(slide.forcedSpeed, 2.75F) && near(slide.canTransition, 0.5F), "the slide of a sprint to crouch");
	check(move::transition_of(Posture::crouch, Posture::stand, false, true).instant(), "crouch to stand while moving: at once");
	check(near(move::capsule_half_height(Posture::crouch), 58.0F) && near(move::capsule_half_height(Posture::prone), 42.0F),
		"the capsule of crouch and prone");
}

void autorun_tests()
{
	move::AutoRun run;
	check(!run.on() && run.toggle() && run.on(), "the key = starts autorun");
	const auto [side, forward] = run.input(-1.0F, 0.0F);
	check(near(side, -1.0F) && near(forward, 1.0F), "autorun: the forward axis 1, A and D stay");
	run.cancel();
	check(!run.on(), "an end of the data (the back key, a new W, an aim, an attack)");
	run.toggle();
	check(!run.toggle(), "the key = again ends it");
}

/** The height of a position on the wire (protocol notes 4.7: 0 = -100 m, 0xFFFF = 1000 m). */
std::uint16_t wire_height(float z)
{
	return static_cast<std::uint16_t>(std::lround((z + 100.0F) / 1100.0F * 65535.0F));
}

/**
 * A client at 30 steps in a second that moves in a straight line with the motion of the rules (each axis to its target with
 * the acceleration, as SwcCharacterMovement::CalcVelocity) for `seconds`, then stops; each step one report into the position
 * check of the backend. @return The count of the refused reports.
 */
std::uint64_t run_reports(float speed, float accel, float decel, float seconds, float speedFactor)
{
	backend::world::PositionTrack track;
	const auto start = backend::world::WorldClock::time_point{} + std::chrono::hours(1);
	backend::state::WorldPosition position{9000.0F, 4500.0F, 130.0F};
	const backend::state::WorldPosition known = position;
	float velocity = 0.0F;
	const float dt = 1.0F / 30.0F;
	for (int step = 0; step < static_cast<int>((seconds + 2.0F) * 30.0F); ++step) {
		const float target = step < seconds * 30.0F ? speed : 0.0F;
		velocity = velocity < target ? std::min(velocity + accel * dt, target) : std::max(velocity - decel * dt, target);
		position.x += velocity * dt;
		backend::world::PositionReport report;
		report.input = static_cast<std::uint16_t>(100 + step);
		report.position = position;
		report.height = wire_height(position.z);
		const auto now = start + std::chrono::microseconds(static_cast<long long>(step * 1e6 / 30.0));
		(void)track.judge(report, known, now, speedFactor);
	}
	return track.refused();
}

void position_tests(const move::Data& data)
{
	using move::Posture;
	// The speeds of the rules with the multipliers of the speed table: the backend accepts each report (its window speed 8 m/s
	// times the multiplier that it sent; the session gives it the table value, here speedFactor).
	struct Case {
		const char* what;
		move::Posture posture;
		float multiplier;
	};
	for (const Case& item : {Case{"the sprint", Posture::stand, 1.0F}, Case{"the road sprint (the table 1.15)", Posture::stand, 1.15F},
			 Case{"the haste elixir (the table 1.4)", Posture::stand, 1.4F}, Case{"the largest table (1.5)", Posture::stand, 1.5F},
			 Case{"the slowed sprint (the table 0.5)", Posture::stand, 0.5F}, Case{"the crouch sprint", Posture::crouch, 1.0F},
			 Case{"the prone sprint", Posture::prone, 1.0F}}) {
		const move::Motion* sprint = move::motion_of(data, "Sprint", facts_of(item.posture));
		const float factor = std::clamp(item.multiplier, 0.0F, 1.5F);
		const std::uint64_t refused = run_reports(sprint->speed[1] * factor, sprint->accel[1] * factor, sprint->decel[1], 10.0F, std::max(factor, 1.0F));
		check(refused == 0, std::string(item.what) + ": 10 s of reports, " + std::to_string(refused) + " refused");
	}
	// The slide of a sprint to crouch: 2.75 m/s forced, then the crouch sprint.
	check(run_reports(2.75F, 8.0F, 10.0F, 2.0F, 1.0F) == 0, "the slide: no report refused");
	// A control: a client faster than the window speed and its margin (8 m/s and 2 m in a window of 1 s) is refused.
	check(run_reports(11.0F, 40.0F, 40.0F, 5.0F, 1.0F) > 0, "a control: 11 m/s is refused");
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: swc-movement-tests MOVEMENT_JSON\n");
		return 2;
	}
	std::ifstream file(argv[1]);
	if (!file) {
		std::fprintf(stderr, "no %s (tools/convert-player.sh movement)\n", argv[1]);
		return 2;
	}
	const move::Data data = data_of(Json::parse(file));
	speed_tests(data);
	limit_tests(data);
	posture_tests();
	autorun_tests();
	position_tests(data);
	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed == 0 ? 0 : 1;
}
