// The parity of the presses of the ranged and magic weapons (stage P3.6): the attack driver of the client (Combat/SwcAttackGraph
// with the ranged programs of actions.json) makes the input entries of scripted presses (the action bits and the shot items of list
// 2), and the world of the backend takes them through its input reader and its shot rules (world/projectiles.cpp
// Projectiles::launch: the press of the item, the release window, the charge of a heavy shot, the shot rule of the data, the
// ammunition, the origin, the direction) with the real projectile tables of the backend copy (projectile-data.json). Each shot of
// the client must be accepted, and a wolf 20 m to the east must take the hit. The backend is the core library of the Soulwarden
// sources (read only) in SWC_WORK/build/backend-core. Build and run: tools/test-ranged.sh.

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "Combat/SwcAttackGraph.h"
#include "ranged_load.h"
#include "support/projectile_fixture.h"

namespace {

namespace backend = soulwarden::backend;
namespace shop = backend::tests::shop;
namespace gamedata = backend::gamedata;
namespace world = backend::world;

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& text)
{
	++g_checks;
	if (!condition) {
		++g_failures;
		std::printf("  FAIL %s\n", text.c_str());
	}
}

/**
 * The world of the cases: the shop of the backend tests with a bow, a fire staff, and a life staff whose weapon rows have the profiles
 * of the game (Bow, FireBall, LifeStaff), the damage rows of the basic attacks, and the projectile tables of the backend copy.
 */
struct RealShop : shop::Shop {
	explicit RealShop(const std::string& projectileData) : shop::Shop(false, false, false)
	{
		gamedata::CombatData combat = shop::shop_combat();
		const auto row = [&](const char* id, const char* kind, std::uint32_t type, float factor) {
			combat.damageRows.push_back(gamedata::DamageRow{id, kind, type, factor, 0.0F, 0.0F, 0.0F});
			return static_cast<std::uint32_t>(combat.damageRows.size() - 1);
		};
		const std::uint32_t light = row("BowAttack1", "Light", 1, 1.0F);
		const std::uint32_t heavy = row("BowAttack2", "Heavy", 1, 1.5F);
		row("BowAttack_Hipshot", "Light", 1, 0.6F);
		const std::uint32_t fire = row("Firestaff_PrimaryAttackC1", "Light", 4, 1.0F);
		const std::uint32_t fireHeavy = row("Firestaff_PrimaryAttackC2", "Heavy", 4, 1.4F);
		const std::uint32_t life = row("LifeStaff_PrimaryAttackC1", "Light", 4, 1.07F);
		const std::uint32_t lifeHeavy = row("LifeStaff_PrimaryAttackC2", "Heavy", 4, 1.51F);
		gamedata::WeaponRow bow = combat.weapons.front();
		bow.id = "TestBowRow";
		bow.melee = false;
		bow.lightRow = light;
		bow.heavyRow = heavy;
		bow.mastery = "BowAbilityTable";
		gamedata::WeaponRow staff = bow;
		staff.id = "TestStaffRow";
		staff.lightRow = fire;
		staff.heavyRow = fireHeavy;
		staff.mastery = "FireMagicAbilityTable";
		gamedata::WeaponRow lifeStaff = bow;
		lifeStaff.id = "TestLifeRow";
		lifeStaff.lightRow = life;
		lifeStaff.heavyRow = lifeHeavy;
		lifeStaff.mastery = "LifeMagicAbilityTable";
		for (const auto& [item, weapon] : {std::pair{"testbow", bow}, std::pair{"teststaff", staff}, std::pair{"testlifestaff", lifeStaff}}) {
			combat.weapons.push_back(weapon);
			combat.items[item] = static_cast<std::uint32_t>(combat.weapons.size() - 1);
		}
		if (!host.combat.set(combat)) {
			throw std::runtime_error("parity test: the combat tables are not valid: " + host.combat.error());
		}
		std::vector<gamedata::ItemDefinition> items = shop::shop_catalog();
		items.push_back(shop::definition("TestBow", "Weapon", "Weapon+EquippableTwoHand+Bow+Ranged", 5, 100, 600));
		items.push_back(shop::definition("TestStaff", "Weapon", "Weapon+EquippableTwoHand+MagicStaff", 5, 100, 600));
		items.push_back(shop::definition("TestLifeStaff", "Weapon", "Weapon+EquippableTwoHand+MagicStaff", 5, 100, 600));
		host.catalog.set(items);
		// The projectile tables of the backend copy, with the weapon rows of the test items (the profiles of the game).
		gamedata::ProjectileData loaded;
		if (!loaded.load(projectileData)) {
			throw std::runtime_error("parity test: no projectile data at " + projectileData + ": " + loaded.error());
		}
		gamedata::ProjectileTables tables = loaded.tables();
		tables.weapons.push_back({"TestBowRow", "Bow", "Arrow"});
		tables.weapons.push_back({"TestStaffRow", "FireBall", ""});
		tables.weapons.push_back({"TestLifeRow", "LifeStaff", ""});
		if (!host.projectiles.set(tables)) {
			throw std::runtime_error("parity test: the projectile tables are not valid: " + host.projectiles.error());
		}
		if (!host.creatures.set_full({backend::tests::projectile::capsule_wolf()}, {shop::wolf_kind()}, {}, {{1, shop::kWolfHealth * 100.0F, 100.0F, 50.0F}})) {
			throw std::runtime_error("parity test: the creature data is not valid");
		}
	}
	void hold(const char* item)
	{
		if (!wear(item, world::paperdoll::kMainHand1).ok()) {
			throw std::runtime_error(std::string("parity test: the item ") + item);
		}
		character()->combat.selectedWeapon = world::paperdoll::kMainHand1;
	}
	const world::ProjectileCounts& counts() { return host.world.projectiles().counts(); }
};

/** A script: the left button and the right button over the steps (one event of each in a step). */
struct Script {
	std::string name;
	std::vector<std::pair<int, bool>> button;
	bool aim{false};
	int steps{80};
	/** The shots that the client must send, and the heavy ones among them. */
	unsigned shots{1};
};

/** Runs a script: the driver steps at the input numbers of the walker; each entry has its bits and its shot items. */
unsigned run(RealShop& world, swc::combat::Driver& driver, const Script& script, unsigned& sent)
{
	unsigned items = 0;
	driver.set_aim(script.aim);
	for (int step = 0; step < script.steps; ++step) {
		for (const auto& [at, down] : script.button) {
			if (at == step) {
				driver.button(down);
			}
		}
		backend::tests::InputEntry entry = world.walker.entry();
		std::vector<unsigned> bits;
		std::vector<swc::combat::Shot> shots;
		driver.step(world.walker.input, static_cast<std::int64_t>(world.walker.input) * 33'333'333LL, bits, &shots);
		entry.actionBits = bits;
		for (const swc::combat::Shot& shot : shots) {
			// The shot item of RepClient: the origin `forward` m along the look (east here) and `up` m above the feet, the direction to
			// the aim point: the body of the wolf 20 m east, 0.5 m above the ground (a magic shot has no gravity).
			backend::tests::InputShotItem item;
			item.origin = {world.walker.position.x + shot.forward, world.walker.position.y, world.walker.position.z + shot.up};
			const float dx = shop::kPlace.x + 20.0F - item.origin[0];
			const float dz = shop::kHeight + 0.5F - item.origin[2];
			const float length = std::sqrt(dx * dx + dz * dz);
			item.direction = {dx / length, 0.0F, dz / length};
			item.attack = shot.attack;
			item.row = shot.row;
			item.charge = shot.charge;
			item.ammo = shot.ammo;
			entry.shotItems.push_back(item);
			++items;
		}
		world.walker.send(entry);
	}
	sent += items;
	return items;
}

void weapon_cases(const swc::ranged::test::Data& data, const std::string& projectileData, const std::string& key, const char* item,
	const std::vector<Script>& scripts)
{
	const auto found = data.programs.find(key);
	check(found != data.programs.end(), key + ": the program");
	if (found == data.programs.end()) {
		return;
	}
	RealShop world(projectileData);
	world.hold(item);
	world::Entity* wolf = world.host.world.creatures().spawn("Test_Wolf", shop::at(shop::kPlace.x + 20.0F, shop::kPlace.y), 90.0F).entity;
	check(wolf != nullptr, key + ": a wolf 20 m east");
	world.walker.idle(20);
	swc::combat::Driver driver;
	driver.set_weapons(nullptr, nullptr, swc::combat::kMainHand1);
	driver.set_ranged(&found->second, nullptr, &data.shared);
	unsigned sent = 0;
	for (const Script& script : scripts) {
		const std::uint64_t accepted = world.counts().shots[static_cast<std::size_t>(world::ShotVerdict::accepted)];
		const unsigned items = run(world, driver, script, sent);
		const std::uint64_t now = world.counts().shots[static_cast<std::size_t>(world::ShotVerdict::accepted)];
		check(items == script.shots, key + " " + script.name + ": the client sends " + std::to_string(script.shots) + " shot item(s) (" + std::to_string(items) + ")");
		check(now - accepted == items, key + " " + script.name + ": the backend accepts each one (" + std::to_string(now - accepted) + " of " + std::to_string(items) + ")");
		world.walker.idle(30);
	}
	const world::ProjectileCounts& counts = world.counts();
	std::uint64_t refused = 0;
	std::string causes;
	for (std::size_t verdict = 1; verdict < counts.shots.size(); ++verdict) {
		refused += counts.shots[verdict];
		if (counts.shots[verdict] != 0) {
			causes += " " + std::to_string(verdict) + ":" + std::to_string(counts.shots[verdict]);
		}
	}
	check(refused == 0, key + ": no shot refused" + causes);
	const std::uint64_t hits = counts.ends[static_cast<std::size_t>(world::ProjectileEnd::hit)];
	check(hits == sent, key + ": each projectile hits the wolf (" + std::to_string(hits) + " of " + std::to_string(sent) + ")");
	std::string ends;
	for (std::size_t end = 0; end < counts.ends.size(); ++end) {
		ends += " " + std::to_string(counts.ends[end]);
	}
	std::printf("  %s: %u shot(s) sent, %llu accepted, %llu hit(s); the ends (hit, near hit, wall, range, life, caster, absorbed):%s\n", key.c_str(), sent,
		static_cast<unsigned long long>(counts.shots[static_cast<std::size_t>(world::ShotVerdict::accepted)]), static_cast<unsigned long long>(hits),
		ends.c_str());
}

/** The control: a charged item after 5 steps of hold (the client never sends it) must be refused (badCharge), else the test sees nothing. */
void control_case(const std::string& projectileData)
{
	RealShop world(projectileData);
	world.hold("TestBow");
	world.walker.bits({swc::combat::kBitPress, swc::combat::kBitPressVirtual, swc::combat::kBitShoot});
	for (int step = 0; step < 4; ++step) {
		world.walker.bits({swc::combat::kBitAimShootHold});
	}
	backend::tests::InputEntry entry = world.walker.entry({swc::combat::kBitRelease});
	backend::tests::InputShotItem item;
	item.origin = {world.walker.position.x + 1.0F, world.walker.position.y, world.walker.position.z + 1.46F};
	item.direction = {1.0F, 0.0F, 0.0F};
	item.attack = backend::gamedata::progression_id("basic_heavy");
	item.row = backend::gamedata::progression_id("bowattack2");
	item.charge = 100.0F;
	item.ammo = backend::gamedata::progression_id("defaultarrow");
	entry.shotItems = {item};
	world.walker.send(entry);
	check(world.counts().shots[static_cast<std::size_t>(world::ShotVerdict::badCharge)] == 1,
		"the control: a charged item after 5 steps of hold is refused (badCharge)");
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 3) {
		std::fprintf(stderr, "usage: swc-ranged-parity-tests ACTIONS_JSON PROJECTILE_DATA_JSON\n");
		return 2;
	}
	swc::ranged::test::Data data;
	if (!swc::ranged::test::load(argv[1], data)) {
		std::fprintf(stderr, "no file %s\n", argv[1]);
		return 2;
	}
	std::printf("the presses of the client in the world of the backend\n");
	const auto click = [](int at) { return std::vector<std::pair<int, bool>>{{at, true}, {at + 1, false}}; };
	const auto hold = [](int at, int steps) { return std::vector<std::pair<int, bool>>{{at, true}, {at + steps, false}}; };
	try {
		// The bow: the hip shot (a click with no aim), the aimed light shot (3 steps), the charged shot (20 steps; the backend needs 14).
		weapon_cases(data, argv[2], "Bow,LongBow", "TestBow",
			{{"hip shot", click(5), false, 70, 1}, {"aimed light shot", hold(30, 3), true, 90, 1}, {"charged shot", hold(30, 20), true, 110, 1}});
		// The fire staff and the life staff: the light cast (a click), the heavy cast (a hold to 0.6667 s; the backend needs 4 steps).
		weapon_cases(data, argv[2], "MagicStaff,Fire", "TestStaff", {{"light cast", click(5), false, 60, 1}, {"heavy cast", hold(5, 24), false, 70, 1}});
		weapon_cases(data, argv[2], "MagicStaff,Heal", "TestLifeStaff", {{"light cast", click(5), false, 60, 1}, {"heavy cast", hold(5, 24), false, 70, 1}});
		control_case(argv[2]);
	} catch (const std::exception& error) {
		check(false, error.what());
	}
	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
