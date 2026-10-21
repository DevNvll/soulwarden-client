#pragma once

#include <cstdint>
#include <set>
#include <string_view>

#include "Combat/SwcAttackGraph.h"

// The supported-action list of the fixed backend (stage MVP-A, Linear AST-214; docs/ACTIONS.md "The supported-action list"). The
// client plays, sends, and shows an action only when the backend has a rule for it: an action with no rule gives no false success,
// heal, cooldown, or damage result. The list is a copy of the backend source (read only): world/abilities.cpp kRules (the abilities
// with a cast plan) and world/runes.h kDetonateAbility (the one heart rune activation). tools/test-actions.sh compares this copy with
// kRules of the backend source.

namespace swc::combat {

/** The active abilities with a cast plan of the backend (world/abilities.cpp kRules), in the sequence of that list. The six abilities
 *  of Ability_Greataxe_Reap to Ability_WarHammer_ClearOut need their fragments in the weapon actions (format 2) of the backend copy. */
inline constexpr std::string_view kBackendAbilities[] = {
	"Ability_Greataxe_Charge",
	"Ability_Greataxe_Maelstorm",
	"Ability_Greataxe_GravityWell",
	"Ability_WarHammer_WreckingBall",
	"Ability_WarHammer_Shockwave",
	"Ability_WarHammer_PathOfDestiny",
	"Ability_Rapier_Riposte",
	"IceMagic_IcePylon",
	"Ability_Musket_Traps",
	"Ability_Lifestaff_Beacon",
	"Ability_Lifestaff_DivineEmbrace",
	"Ability_Lifestaff_SacredGround",
	"Ability_Greataxe_Reap",
	"Ability_Greataxe_Executioner",
	"Ability_Greataxe_Whirlwind",
	"Ability_WarHammer_MightyGavel",
	"Ability_Warhammer_ArmorBreaker",
	"Ability_WarHammer_ClearOut",
	"Ability_Bow_PenetratingShot",
	"Ability_Bow_SplinterShot",
	"Ability_Bow_RapidShot",
};

/** One active ability of the five weapons of the MVP (the active rows of the ability tables of the game data). */
struct WeaponAbility {
	std::string_view weapon;
	std::string_view id;
};

/** The 30 active abilities of the five weapons of the MVP. `ability_supported` tells which of them the backend plays. */
inline constexpr WeaponAbility kWeaponAbilities[] = {
	{"sword", "Ability_Sword_ReverseStab"},
	{"sword", "Ability_Sword_WhirlingBlade"},
	{"sword", "Ability_Sword_LeapingStrike"},
	{"sword", "Ability_Sword_Bash"},
	{"sword", "Ability_Sword_Rush"},
	{"sword", "Ability_Sword_FinalStand"},
	{"rapier", "Ability_Rapier_Slash"},
	{"rapier", "Ability_Rapier_Flurry"},
	{"rapier", "Ability_Rapier_Flourish"},
	{"rapier", "Ability_Rapier_Riposte"},
	{"rapier", "Ability_Rapier_Aerial_Lunge_Intro"},
	{"rapier", "Ability_Rapier_Evasion"},
	{"bow", "Ability_Bow_PoisonShot"},
	{"bow", "Ability_Bow_EvadeShot"},
	{"bow", "Ability_Bow_RainofArrows"},
	{"bow", "Ability_Bow_RapidShot"},
	{"bow", "Ability_Bow_PenetratingShot"},
	{"bow", "Ability_Bow_SplinterShot"},
	{"greataxe", "Ability_Greataxe_Charge"},
	{"greataxe", "Ability_Greataxe_Reap"},
	{"greataxe", "Ability_Greataxe_Executioner"},
	{"greataxe", "Ability_Greataxe_Whirlwind"},
	{"greataxe", "Ability_Greataxe_Maelstorm"},
	{"greataxe", "Ability_Greataxe_GravityWell"},
	{"warhammer", "Ability_Warhammer_ArmorBreaker"},
	{"warhammer", "Ability_WarHammer_MightyGavel"},
	{"warhammer", "Ability_WarHammer_WreckingBall"},
	{"warhammer", "Ability_WarHammer_ClearOut"},
	{"warhammer", "Ability_WarHammer_Shockwave"},
	{"warhammer", "Ability_WarHammer_PathOfDestiny"},
};

/** The one heart rune activation of the backend (world/runes.h kDetonateAbility): the EquipAbility of the rune must be this ability.
 *  Bile Bomb and each other rune ability have no rule (docs/systems/passive-rules.md of the backend). */
inline constexpr std::string_view kRuneActivation = "Ability_Rune_Detonate";

/** @return The progression ids of the abilities with a cast plan of the backend. */
inline std::set<std::uint32_t> supported_abilities()
{
	std::set<std::uint32_t> out;
	for (const std::string_view id : kBackendAbilities) {
		out.insert(progression_id(id));
	}
	return out;
}

/** @return True when the backend has a cast plan for the ability with this progression id. */
inline bool ability_supported(std::uint32_t ability)
{
	static const std::set<std::uint32_t> supported = supported_abilities();
	return supported.count(ability) != 0;
}

/** @return True when the backend activates a heart rune with this equip ability (its progression id). */
inline bool rune_supported(std::uint32_t equipAbility)
{
	return equipAbility != 0 && equipAbility == progression_id(kRuneActivation);
}

} // namespace swc::combat
