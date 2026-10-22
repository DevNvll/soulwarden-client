// Tests of the ranged and magic actions of the client (stage P3.6, docs/RANGED.md): the player of the action lists and the
// animation databases of the data (Combat/SwcRanged) with the programs of actions.json (tools/convert-player.sh actions), against
// the step rules of the backend (Soulwarden protocol notes 4.27, 4.28, 4.31, 4.46; backend world/projectiles.cpp,
// world/abilities.cpp) and the live values of the notes. Build and run: tools/test-ranged.sh.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ranged_load.h"

namespace {

namespace ranged = swc::ranged;

int g_checks = 0;
int g_failures = 0;
bool g_trace = false;
/** The leaves of the conditions that the player did not know in all runs (type -> count). */
std::map<std::string, std::uint32_t> g_unknown;

void check(bool condition, const std::string& text)
{
	++g_checks;
	if (!condition) {
		++g_failures;
		std::printf("  FAIL %s\n", text.c_str());
	}
}

std::uint32_t crc(std::string_view text)
{
	std::uint32_t value = 0xFFFFFFFFU;
	for (char c : text) {
		const auto byte = static_cast<std::uint8_t>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
		value ^= byte;
		for (int bit = 0; bit < 8; ++bit) {
			value = (value >> 1) ^ (0xEDB88320U & (0U - (value & 1U)));
		}
	}
	return ~value;
}

/** The events of a run with the step of each one, and the trace of the fragments. */
struct Run {
	std::vector<ranged::Event> events;
	std::vector<std::pair<int, std::string>> fragments;
	std::vector<std::pair<int, std::string>> actions;
	ranged::Player player;

	[[nodiscard]] std::vector<ranged::Event> of(ranged::Event::Kind kind) const
	{
		std::vector<ranged::Event> out;
		for (const ranged::Event& event : events) {
			if (event.kind == kind) {
				out.push_back(event);
			}
		}
		return out;
	}
	[[nodiscard]] int fragment_step(std::string_view name) const
	{
		for (const auto& [step, fragment] : fragments) {
			if (fragment == name) {
				return step;
			}
		}
		return -1;
	}
};

/** The keys of a script: the left button down from `pressAt` to `releaseAt`; the right button from `aimFrom` to `aimTo`. */
struct Script {
	int pressAt{-1};
	int releaseAt{-1};
	int aimFrom{-1};
	int aimTo{-1};
	int keyAt{-1};
	unsigned keyPlace{0};
	int selfFrom{-1};
	int selfTo{-1};
	std::vector<int> clicks;
};

Run run(const ranged::Program& program, const ranged::Shared& shared, const Script& script, int steps, ranged::Facts facts,
	const std::function<void(int, ranged::Facts&)>& change = {})
{
	Run out;
	out.player.set_program(&program, &shared);
	bool attack = false;
	bool aim = false;
	std::string lastFragment;
	std::string lastAction;
	for (int step = 0; step < steps; ++step) {
		ranged::Input in;
		const bool attackNow = (script.pressAt >= 0 && step >= script.pressAt && step < script.releaseAt)
			|| std::find(script.clicks.begin(), script.clicks.end(), step) != script.clicks.end();
		in.attackDown = attackNow;
		in.attackPress = attackNow && !attack;
		in.attackRelease = !attackNow && attack;
		attack = attackNow;
		const bool aimNow = script.aimFrom >= 0 && step >= script.aimFrom && step < script.aimTo;
		in.aimDown = aimNow;
		in.aimPress = aimNow && !aim;
		in.aimRelease = !aimNow && aim;
		aim = aimNow;
		if (step == script.keyAt) {
			in.keyPress[script.keyPlace] = true;
		}
		in.keyDown[script.keyPlace] = step >= script.keyAt && step < script.keyAt + 3;
		in.selfTarget = script.selfFrom >= 0 && step >= script.selfFrom && step < script.selfTo;
		if (change) {
			change(step, facts);
		}
		std::vector<ranged::Event> events;
		out.player.step(static_cast<std::uint16_t>(step), in, facts, events);
		for (ranged::Event& event : events) {
			out.events.push_back(event);
			if (g_trace) {
				std::printf("    %3d event %d %s attack %08x row %08x charge %.0f ammo %08x amount %.1f\n", step, static_cast<int>(event.kind),
					event.text.c_str(), event.attack, event.row, event.charge, event.ammo, event.amount);
			}
		}
		const ranged::Playing& playing = out.player.playing();
		const std::string fragment = playing.fragment != nullptr ? playing.fragment->name + "|" + std::to_string(playing.serial) : "";
		const std::string action = playing.action != nullptr ? playing.action->name : "";
		if (fragment != lastFragment) {
			lastFragment = fragment;
			out.fragments.emplace_back(step, playing.fragment != nullptr ? playing.fragment->name : "");
			if (g_trace) {
				std::printf("    %3d fragment %s (action %s)\n", step, fragment.c_str(), action.c_str());
			}
		}
		if (action != lastAction) {
			lastAction = action;
			out.actions.emplace_back(step, action);
		}
	}
	for (const auto& [type, count] : out.player.unknown()) {
		g_unknown[type] += count;
		if (g_trace) {
			std::printf("    unknown %s: %u\n", type.c_str(), count);
		}
	}
	return out;
}

const ranged::Program* program(const ranged::test::Data& data, const std::string& key)
{
	const auto found = data.programs.find(key);
	return found != data.programs.end() ? &found->second : nullptr;
}

void bow_tests(const ranged::test::Data& data)
{
	std::printf("the bow: the hip shot, the aimed light shot, the charged shot\n");
	const ranged::Program* bow = program(data, "Bow,LongBow");
	check(bow != nullptr && bow->ammo == "DefaultArrow", "the bow program and its ammunition");
	if (bow == nullptr) {
		return;
	}
	ranged::Facts facts;
	// The hip shot: a click (the press, then the release one step later) with no aim.
	{
		Script script;
		script.pressAt = 10;
		script.releaseAt = 11;
		const Run r = run(*bow, data.shared, script, 80, facts);
		const auto shots = r.of(ranged::Event::Kind::shot);
		check(r.fragment_step("Bow_Shoot_HipFire") == 10, "the hip shot starts at the press");
		check(shots.size() == 1, "the hip shot: one shot");
		if (shots.size() == 1) {
			// The CAGE-RangedAttack of Bow_Shoot_HipFire at 0.4 s: the 12th step of the fragment; the live item came 10 steps after the
			// release (protocol notes 4.27).
			check(shots[0].input == 21 && shots[0].input - script.releaseAt == 10, "the hip shot at 0.4 s, 10 steps after the release");
			check(shots[0].attack == crc("Basic") && shots[0].row == crc("BowAttack_Hipshot") && shots[0].charge == 0.0F
					&& shots[0].ammo == crc("DefaultArrow"),
				"the hip shot item: basic, bowattack_hipshot, charge 0, DefaultArrow");
		}
	}
	// The aimed light shot: the right button from step 0; a press at 30, the release at 33.
	{
		Script script;
		script.aimFrom = 0;
		script.aimTo = 120;
		script.pressAt = 30;
		script.releaseAt = 33;
		const Run r = run(*bow, data.shared, script, 100, facts);
		const auto shots = r.of(ranged::Event::Kind::shot);
		check(r.fragment_step("Bow_ReadyWeapon_UpperBody") == 0, "the aim: the ready fragment at the right button");
		// The always transition at 0.4333 s of Bow_ReadyWeapon_UpperBody: the 13th step of the fragment.
		check(r.fragment_step("Bow_Aim_UpperBody") == 12, "the aim loop at 0.4333 s of the ready fragment");
		check(r.player.aim_down_sights(), "the tag AimDownSights in the aim");
		check(shots.size() == 1, "the aimed light shot: one shot");
		if (shots.size() == 1) {
			check(shots[0].input == 33, "the light shot in the step of the release (the item comes with the release bit, notes 4.27)");
			check(shots[0].attack == crc("Basic") && shots[0].row == crc("BowAttack1") && shots[0].charge == 0.0F, "the light item: basic, bowattack1, charge 0");
		}
	}
	// The charged shot: a hold of 20 steps (the mark SetChargedAim at 0.5 s of Bow_Charge_UpperBody).
	{
		Script script;
		script.aimFrom = 0;
		script.aimTo = 140;
		script.pressAt = 30;
		script.releaseAt = 50;
		const Run r = run(*bow, data.shared, script, 120, facts);
		const auto shots = r.of(ranged::Event::Kind::shot);
		check(shots.size() == 1, "the charged shot: one shot");
		if (shots.size() == 1) {
			check(shots[0].input == 50 && shots[0].attack == crc("Basic_Heavy") && shots[0].row == crc("BowAttack2") && shots[0].charge == 100.0F,
				"the charged item at the release: basic_heavy, bowattack2, charge 100");
			// The heavy rule of the backend: 14 steps from the press to the release (projectile_rules::kChargedSteps).
			check(shots[0].input - script.pressAt >= 14, "the charged hold is 14 steps or more");
		}
	}
}

/** AST-254: the bow pose after the release of the buttons (the play report "the pose stays when I release the mouse button"). Each
 *  case ends with no action and no tag AimDownSights (the aim camera and the aim pose of the client); the log case of the report: the
 *  right button released in the charged shot and pressed again in the lower fragment starts the ready fragment at the mark
 *  Exit_BowAimShoot. Before the fix, the end of the lower action at that mark stopped the fragment before Bow_ReadyWeapon and
 *  Bow_AimChargeShoot_SubState read it: they stayed installed and set Aim and AimDownSights again. */
void bow_release_tests(const ranged::test::Data& data)
{
	std::printf("the bow: the releases of the buttons (AST-254)\n");
	const ranged::Program* bow = program(data, "Bow,LongBow");
	if (bow == nullptr) {
		return;
	}
	ranged::Facts facts;
	const auto cases = std::vector<std::pair<const char*, Script>>{
		{"the right button, released", [] { Script s; s.aimFrom = 0; s.aimTo = 60; return s; }()},
		{"the left button held (charge) with no aim, released", [] { Script s; s.pressAt = 10; s.releaseAt = 40; return s; }()},
		{"both, the right released after the shot", [] { Script s; s.aimFrom = 0; s.aimTo = 80; s.pressAt = 30; s.releaseAt = 50; return s; }()},
		{"both, the right released in the charged shot", [] { Script s; s.aimFrom = 0; s.aimTo = 52; s.pressAt = 30; s.releaseAt = 50; return s; }()},
	};
	for (const auto& [name, script] : cases) {
		const Run r = run(*bow, data.shared, script, 200, facts);
		check(r.player.playing().action == nullptr && !r.player.aim_down_sights() && r.player.tags().count("aim") == 0,
			std::string(name) + ": no action and no aim tags at the end");
	}
	// The log case of 12:50:22 (play log of 2026-10-22): the right button released in the charged shot and pressed again at step 70 (in
	// the lower fragment, before its mark Exit_BowAimShoot at 0.3667 s), held to step 200.
	ranged::Player player;
	player.set_program(bow, &data.shared);
	bool attack = false;
	bool aim = false;
	int readyAgain = -1;
	for (int step = 0; step < 260; ++step) {
		ranged::Input in;
		const bool attackNow = step >= 30 && step < 50;
		const bool aimNow = step < 52 || (step >= 70 && step < 200);
		in.attackDown = attackNow;
		in.attackPress = attackNow && !attack;
		in.attackRelease = !attackNow && attack;
		attack = attackNow;
		in.aimDown = aimNow;
		in.aimPress = aimNow && !aim;
		in.aimRelease = !aimNow && aim;
		aim = aimNow;
		std::vector<ranged::Event> events;
		player.step(static_cast<std::uint16_t>(step), in, facts, events);
		if (step > 64 && readyAgain < 0 && player.playing().action != nullptr && player.playing().action->name == "Bow_ReadyWeapon") {
			readyAgain = step;
		}
	}
	check(readyAgain == 74, "the second press: the ready fragment again at the mark Exit_BowAimShoot of the lower (step 74)");
	check(player.playing().action == nullptr && !player.aim_down_sights(), "the second release: no action and no aim at the end");
}

void staff_tests(const ranged::test::Data& data)
{
	std::printf("the fire staff and the life staff: the light and the heavy casts\n");
	for (const std::string key : {"MagicStaff,Fire", "MagicStaff,Heal"}) {
		const ranged::Program* staff = program(data, key);
		check(staff != nullptr && staff->ammo.empty(), key + ": the program, no ammunition");
		if (staff == nullptr) {
			continue;
		}
		const bool fire = key == "MagicStaff,Fire";
		ranged::Facts facts;
		{
			Script script;
			script.pressAt = 10;
			script.releaseAt = 11;
			const Run r = run(*staff, data.shared, script, 80, facts);
			const auto shots = r.of(ranged::Event::Kind::shot);
			check(r.fragment_step("Attack_Start_Charge_Upperbody") == 10, key + ": the charge start at the press");
			check(shots.size() == 1, key + ": the light cast: one shot");
			if (shots.size() == 1) {
				check(shots[0].attack == crc(fire ? "Basic" : "Primary") && shots[0].row == 0 && shots[0].charge == 0.0F && shots[0].ammo == 0,
					key + ": the light item (row 0, charge 0, no ammunition)");
				// The mark of the cast at 0.1 s of the cast fragment; the live fire ball came 5 or 6 steps after the press (notes 4.27).
				check(shots[0].input >= 12 && shots[0].input <= 16, key + ": the light shot " + std::to_string(shots[0].input - 10) + " steps after the press");
			}
		}
		{
			// A hold of 50 steps: the heavy cast at 0.6667 s of the charge, then the button that stays down starts the next charge (the
			// instant window of CanAttack_Light_Magic: attack_primary_virtual), and its release gives a light cast.
			Script script;
			script.pressAt = 10;
			script.releaseAt = 60;
			const Run r = run(*staff, data.shared, script, 100, facts);
			const auto shots = r.of(ranged::Event::Kind::shot);
			check(shots.size() == 2, key + ": a long hold: the heavy cast, then the next charge");
			if (shots.size() == 2) {
				check(shots[0].attack == crc(fire ? "Charged" : "Heavy") && shots[0].input == 31, key + ": the heavy item at 0.6667 s + the mark 0.1 s");
				// The heavy rule of the backend for a magic weapon: 4 steps of hold (projectile_rules::kMagicHeavySteps).
				check(shots[0].input - 10 >= 4, key + ": the heavy hold is 4 steps or more");
				check(shots[1].attack == crc(fire ? "Basic" : "Primary") && shots[1].input > 60, key + ": the light item after the release");
			}
		}
	}
}

ranged::Facts with_bar(std::uint32_t ability, float mana = 100.0F)
{
	ranged::Facts facts;
	facts.bar = {ability, 0, 0};
	facts.mana = mana;
	facts.groundValid = true;
	return facts;
}

void ability_tests(const ranged::test::Data& data)
{
	std::printf("the abilities: the casts, the shots, the aims and their confirm, the costs, the cooldowns\n");
	const ranged::Program* bow = program(data, "Bow,LongBow");
	const ranged::Program* fire = program(data, "MagicStaff,Fire");
	const ranged::Program* life = program(data, "MagicStaff,Heal");
	if (bow == nullptr || fire == nullptr || life == nullptr) {
		check(false, "the three programs");
		return;
	}
	const auto key_at = [](int step) {
		Script script;
		script.keyAt = step;
		return script;
	};
	// The Beacon: the projectile at 0.1667 s, the 4th step after the key (live: 4 steps, protocol notes 4.28), its cost 16.
	{
		const Run r = run(*life, data.shared, key_at(10), 80, with_bar(crc("Ability_Lifestaff_Beacon")));
		const auto shots = r.of(ranged::Event::Kind::shot);
		const auto mana = r.of(ranged::Event::Kind::mana);
		const auto cooldowns = r.of(ranged::Event::Kind::cooldown);
		check(!r.of(ranged::Event::Kind::ability).empty(), "the Beacon starts at its key");
		check(shots.size() == 1 && shots[0].input == 14 && shots[0].attack == crc("Heavy"), "the Beacon: the shot heavy 4 steps after the key");
		check(mana.size() == 1 && mana[0].amount == 16.0F, "the Beacon: the cost 16 (LifeStaff_Beacon)");
		check(cooldowns.size() == 1 && cooldowns[0].ability == crc("Ability_Lifestaff_Beacon"), "the Beacon: its cooldown");
	}
	// In its cooldown, or with too little mana: no start.
	{
		ranged::Facts facts = with_bar(crc("Ability_Lifestaff_Beacon"));
		facts.cooldowns.insert(crc("Ability_Lifestaff_Beacon"));
		const Run r = run(*life, data.shared, key_at(10), 40, facts);
		check(r.of(ranged::Event::Kind::shot).empty() && r.of(ranged::Event::Kind::ability).empty(), "the Beacon in its cooldown: no start");
		const Run poor = run(*life, data.shared, key_at(10), 40, with_bar(crc("Ability_Lifestaff_Beacon"), 10.0F));
		check(poor.of(ranged::Event::Kind::ability).empty(), "the Beacon with 10 mana: no start (IfManaCost LifeStaff_Beacon 16)");
	}
	// Sacred Ground: the key starts the aim (the ground target of the spell), a click confirms it; the cast at 0.3333 s of the loop.
	{
		Script script = key_at(10);
		script.clicks = {30};
		ranged::Facts facts = with_bar(crc("Ability_Lifestaff_SacredGround"));
		std::string targeting;
		Run r = run(*life, data.shared, script, 80, facts);
		const auto casts = r.of(ranged::Event::Kind::cast);
		const auto raycasts = r.of(ranged::Event::Kind::raycast);
		check(!raycasts.empty() && raycasts[0].passed, "Sacred Ground: the raycast of the confirm");
		check(casts.size() == 1 && casts[0].text.rfind("Spell_LifeStaff_SacredGround", 0) == 0, "Sacred Ground: one cast of its spell");
		if (casts.size() == 1 && !raycasts.empty()) {
			check(casts[0].input > raycasts[0].input, "Sacred Ground: the cast after the confirm");
		}
		const auto mana = r.of(ranged::Event::Kind::mana);
		check(mana.size() == 1 && mana[0].amount == 15.0F, "Sacred Ground: the cost 15 (LifeStaff_HealingWave)");
	}
	// The cooldown state of the server for Sacred Ground comes one step after the confirm (the backend starts the cooldown at the confirm):
	// the cast starts in the step of the raycast, before that state (the live run p36-life lost the cast with a later start).
	{
		Script script = key_at(10);
		script.clicks = {30};
		const Run r = run(*life, data.shared, script, 80, with_bar(crc("Ability_Lifestaff_SacredGround")), [](int step, ranged::Facts& facts) {
			if (step >= 31) {
				facts.cooldowns.insert(crc("Ability_Lifestaff_SacredGround"));
			}
		});
		const auto casts = r.of(ranged::Event::Kind::cast);
		check(casts.size() == 1 && r.fragment_step("Ability_Life_Staff_HealingWave_Loop") == 30,
			"Sacred Ground: the cast loop in the step of the confirm, before the cooldown state of the server");
	}
	// The aim of Sacred Ground with no confirm: the targeting spell while it lasts, no cast.
	{
		ranged::Player player;
		player.set_program(life, &data.shared);
		const ranged::Facts facts = with_bar(crc("Ability_Lifestaff_SacredGround"));
		std::vector<ranged::Event> events;
		std::string seen;
		for (int step = 0; step < 40; ++step) {
			ranged::Input in;
			in.keyPress[0] = step == 10;
			player.step(static_cast<std::uint16_t>(step), in, facts, events);
			if (!player.targeting().empty()) {
				seen = player.targeting();
			}
		}
		check(seen.rfind("Spell_LifeStaff_SacredGround", 0) == 0, "Sacred Ground: the ground target of its spell in the aim (" + seen + ")");
		check(std::none_of(events.begin(), events.end(), [](const ranged::Event& e) { return e.kind == ranged::Event::Kind::cast; }), "Sacred Ground: no cast with no confirm");
	}
	// Divine Embrace with Left Ctrl held: the self cast at once, the spell at 1.0 s.
	{
		Script script = key_at(10);
		script.selfFrom = 5;
		script.selfTo = 20;
		const Run r = run(*life, data.shared, script, 80, with_bar(crc("Ability_Lifestaff_DivineEmbrace")));
		const auto casts = r.of(ranged::Event::Kind::cast);
		check(casts.size() == 1 && casts[0].text.find("_Self") != std::string::npos, "Divine Embrace with Left Ctrl: the self spell");
		if (casts.size() == 1) {
			check(casts[0].input == 39, "Divine Embrace: the cast at 1.0 s (" + std::to_string(casts[0].input - 10) + " steps)");
		}
	}
	// The bow: Explosive Arrow 13 steps after the key (live), Rapid Shot 3 items 9, 25, and 52 steps after the key (live, notes 4.46).
	{
		// The cast item has the condition Bow_ExplosiveArrow_NoUpgrade: the ability is a learned node (IfAbilityActive).
		ranged::Facts facts = with_bar(crc("Ability_Bow_SplinterShot"));
		facts.nodes.insert(crc("Ability_Bow_SplinterShot"));
		const Run r = run(*bow, data.shared, key_at(10), 60, facts);
		const auto shots = r.of(ranged::Event::Kind::shot);
		check(shots.size() == 1 && shots[0].attack == crc("ExplosionShot"), "Explosive Arrow: one shot explosionshot");
		if (shots.size() == 1) {
			check(shots[0].input - 10 == 13, "Explosive Arrow: 13 steps after the key (" + std::to_string(shots[0].input - 10) + ")");
		}
	}
	{
		const Run r = run(*bow, data.shared, key_at(10), 120, with_bar(crc("Ability_Bow_RapidShot")));
		const auto shots = r.of(ranged::Event::Kind::shot);
		check(shots.size() == 3, "Rapid Shot: three shots");
		if (shots.size() == 3) {
			check(shots[0].attack == crc("TripleShot") && shots[0].row == crc("Bow_RapidAttack_Standard") && shots[2].row == crc("Bow_RapidAttack_KnockBack"),
				"Rapid Shot: tripleshot, standard, standard, knockback");
			const int a = shots[0].input - 10;
			const int b = shots[1].input - 10;
			const int c = shots[2].input - 10;
			check(a == 9 && b == 25 && std::abs(c - 52) <= 1, "Rapid Shot: the steps " + std::to_string(a) + ", " + std::to_string(b) + ", " + std::to_string(c));
		}
	}
	// Penetrating Shot: the key starts the aim; the click shoots (the shot item confirms the aim in the backend).
	{
		Script script = key_at(10);
		script.clicks = {40};
		const Run r = run(*bow, data.shared, script, 90, with_bar(crc("Ability_Bow_PenetratingShot")));
		const auto shots = r.of(ranged::Event::Kind::shot);
		check(shots.size() == 1 && shots[0].attack == crc("PenetratingShot") && shots[0].row == crc("Bow_Penetrating_Attack"),
			"Penetrating Shot: the shot penetratingshot at the confirm");
		const auto cooldowns = r.of(ranged::Event::Kind::cooldown);
		check(!cooldowns.empty() && !shots.empty() && cooldowns[0].input >= shots[0].input - 1, "Penetrating Shot: the cooldown at the shot, not at the key");
	}
	// The fire staff: Pillar of Fire (aim, ground target, confirm, the cast at 0.2333 s), its cost 15.
	{
		Script script = key_at(10);
		script.clicks = {30};
		const Run r = run(*fire, data.shared, script, 90, with_bar(crc("Ability_Firestaff_PillarofFire")));
		const auto casts = r.of(ranged::Event::Kind::cast);
		check(casts.size() == 1 && casts[0].text.rfind("FireStaff_PillarOfFire", 0) == 0, "Pillar of Fire: one cast at the confirm");
		const auto mana = r.of(ranged::Event::Kind::mana);
		check(mana.size() == 1 && mana[0].amount == 15.0F, "Pillar of Fire: the cost 15");
	}
}

void mana_tests()
{
	std::printf("the mana: the regeneration, the payments, the delay at 0, the server copy\n");
	ranged::Mana mana;
	mana.set_server(0.0, 100.0F, 100.0F, 0.0F, 0.0F);
	check(mana.pay(1.0, 16.0F) && std::fabs(mana.value() - 84.0F) < 1e-3F, "a payment of 16");
	mana.advance(3.0);
	check(std::fabs(mana.value() - 88.0F) < 1e-3F, "the regeneration of 2 a second (ManaBaseRegenRate)");
	mana.set_factor(1.2F);
	mana.advance(4.0);
	check(std::fabs(mana.value() - 90.4F) < 1e-3F, "the rate factor of message 4289");
	check(!mana.pay(4.0, 95.0F) && std::fabs(mana.value() - 90.4F) < 1e-3F, "a payment above the mana: nothing paid");
	check(mana.pay(4.0, 90.4F) && mana.value() <= 0.0F, "a payment to 0");
	mana.advance(5.5);
	check(mana.value() <= 0.0F, "no regeneration in the 2 s after a payment to 0 (ManaRegenDelayOnEmpty)");
	mana.advance(7.0);
	check(mana.value() > 0.0F, "the regeneration after the delay");
	// A server copy that differs: taken after 2 s with no payment.
	ranged::Mana copy;
	copy.set_server(0.0, 100.0F, 100.0F, 0.0F, 0.0F);
	copy.set_server(0.5, 50.0F, 100.0F, 0.0F, 0.0F);
	copy.advance(1.0);
	check(copy.value() > 90.0F, "a different server value: not at once");
	copy.advance(3.0);
	copy.advance(3.1);
	check(copy.value() < 60.0F, "a different server value: taken after 2 s");
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: swc-ranged-tests ACTIONS_JSON [--trace]\n");
		return 2;
	}
	g_trace = argc > 2 && std::string(argv[2]) == "--trace";
	ranged::test::Data data;
	if (!ranged::test::load(argv[1], data)) {
		std::fprintf(stderr, "no file %s\n", argv[1]);
		return 2;
	}
	std::printf("programs: %zu, shot rules: %zu, spells: %zu\n", data.programs.size(), data.shared.shots.size(), data.shared.spells.size());
	bow_tests(data);
	bow_release_tests(data);
	staff_tests(data);
	ability_tests(data);
	mana_tests();
	// Each leaf of the conditions has a rule of the client; the named conditions that the action lists do not have are the artifacts of
	// the Grey Wizard Hat, the flash bang shot (not in the bow table), and the target check of Light's Embrace (missingConditions of the
	// converter): false.
	std::printf("the leaves of the conditions\n");
	for (const auto& [type, count] : g_unknown) {
		const bool missing = type.rfind("named:", 0) == 0
			&& (type.find("GreyWizardHatArtifact") != std::string::npos || type == "named:CanAttack_FlashBangShot"
				|| type == "named:Condition_Check_LightsEmbrace_Target_Validity");
		check(missing, "a leaf with no rule: " + type + " (" + std::to_string(count) + ")");
	}
	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
