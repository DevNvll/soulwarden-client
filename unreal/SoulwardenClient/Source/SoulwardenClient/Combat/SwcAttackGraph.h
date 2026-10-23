#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "Combat/SwcRanged.h"
#include "Combat/SwcStamina.h"

// The basic attacks of a melee weapon on the client (stage P2.7): the attack graph of the weapon (the Mannequin fragments
// of its animation databases and the transitions of its action lists, as the backend reads them: weapon-actions.json of
// the backend copy, copied into actions.json by tools/convert-player.sh actions), stepped at the input steps of the
// client (1/30 s), and the weapon switch of the weapon keys. The rules are the rules of the backend (Soulwarden protocol
// notes 4.32; world/attack_sequence.cpp AttackSequences::advance, world/combat.cpp Combat::step_switch): the client plays
// the fragments that the server follows. Plain C++ (no engine type): tools/test-actions.sh compiles it with the backend
// sequence and compares the two.

namespace swc::combat {

/** Input steps in one second (the simulation of the client). */
inline constexpr float kStepsInSecond = 30.0F;
/** A tolerance of the clock comparisons (attack_rules::kClockEpsilon of the backend). */
inline constexpr float kClockEpsilon = 0.0005F;
/** The most transitions in one step (attack_rules::kTransitionsInStep). */
inline constexpr int kTransitionsInStep = 6;
/** The steps of a weapon key (BackTicks 20 of IfInput Use_MainHandSlot_N; switch_rules::kKeySteps). */
inline constexpr unsigned kKeySteps = 20;
/** The first attack of the new weapon starts this number of steps after the switch (grid rule 116; switch_rules). */
inline constexpr unsigned kFirstAttackSteps = 3;

/** The action bits of the attack input (protocol notes 4.15): the press with its virtual and shoot bits, the hold, the release. */
inline constexpr unsigned kBitPress = 555;
inline constexpr unsigned kBitPressVirtual = 203;
inline constexpr unsigned kBitShoot = 408;
inline constexpr unsigned kBitHold = 191;
inline constexpr unsigned kBitRelease = 567;
/** The dodge (left Shift): one step for each press, also for a press that the client refuses (protocol notes 4.38). */
inline constexpr unsigned kBitDodge = 322;
/** The aim input ("aim_draw", the right mouse button with a ranged weapon): each step while it is held. */
inline constexpr unsigned kBitAim = 126;
/** The hold of a bow (aim_shoot_hold): the bow sends it in place of attack_primary_hold (protocol notes 4.27, live; stage P3.6). */
inline constexpr unsigned kBitAimShootHold = 315;
/** The self target (Left Ctrl, self_target): each step while it is down; the self confirm of an aim (backend client_input.cpp). */
inline constexpr unsigned kBitSelfTarget = 430;
/** The weapon keys: Use_MainHandSlot_1 and quickslot-weapon1 (slot 24), Use_MainHandSlot_2 and quickslot-weapon2 (slot 25). */
inline constexpr unsigned kBitsKey1[2] = {9, 40};
inline constexpr unsigned kBitsKey2[2] = {355, 306};
inline constexpr std::uint32_t kMainHand1 = 24;
/**
 * The three keys of the bar (Q, R, F: the places 0 to 2; protocol notes 4.22 "The keys", live proof L1): the first entry of a
 * press has the press bit and the virtual bit, the entries while the key is down the hold bit, one entry the release bit. The
 * backend reads only the press bit (server/rep/client_input.cpp kActionAbility).
 */
inline constexpr unsigned kBitsAbilityPress[3] = {402, 115, 139};
inline constexpr unsigned kBitsAbilityVirtual[3] = {479, 571, 523};
inline constexpr unsigned kBitsAbilityHold[3] = {39, 332, 207};
inline constexpr unsigned kBitsAbilityRelease[3] = {211, 196, 172};
/** The block press (block_press_virtual, the right pointer button): a literal of the transitions inside Charge. */
inline constexpr unsigned kBitBlock = 484;
/**
 * The right pointer button (stage P3.G): the bits of its bindings as the retail client sends them, the same for each weapon
 * (set6-combat/player-bow.json and player-bitten-block.json; the names of the input table, research combat-actions input-bits.json).
 * The step of the press: aim_draw_press_virtual, aim_draw, aim_draw_active_virtual, ui_back_radial, aim_draw_press,
 * aim_draw_release_virtual, block, block_active_virtual, fishing_reset, block_depressed, rotate_character_start, block_press_virtual,
 * rotate_character_start_hold, musicnote_chord_2. Each next step while it is down: aim_draw, aim_draw_active_virtual,
 * block_active_virtual, block_depressed, mount_stop, rotate_character_start_hold, mount_stop_virtual. The step after the release:
 * unbind_key, rotate_character_end, aim_draw_release.
 */
inline constexpr unsigned kBitsRightPress[] = {68, 126, 135, 199, 214, 263, 286, 307, 376, 466, 471, 484, 490, 491};
inline constexpr unsigned kBitsRightHeld[] = {126, 135, 307, 466, 468, 490, 586};
inline constexpr unsigned kBitsRightRelease[] = {183, 202, 562};

/** The input that a transition needs (gamedata::AttackInput of the backend). */
enum class Input : std::uint8_t {
	/** No condition (the full charge). */
	always,
	/** CanAttack_Heavy, CanAttack_Primary: the attack input or its hold; a buffered window takes only a press. */
	virtualInput,
	/** CanAttack_Primary_Rapier: the attack input only. */
	press,
	/** Attack_Heavy_NotHeld: neither the attack input nor its hold. */
	notHeld,
	/** Attack_Primary_Released: the release (or a step with neither the input nor its hold). */
	released,
	/** The start of an ability (a condition CanAttack_<ability>; stage P3.5): a press of the key of that ability in the window
	 *  (Sequence::start_ability), not an input of a step. */
	ability,
	/** A transition inside an ability with the condition of the data as terms (Transition::terms; stage P3.5). */
	terms,
	/** A name that the client does not know: never. */
	other,
};

/** One literal of the condition of a transition inside an ability (gamedata::ConditionLiteral of the backend). */
struct Literal {
	enum class Kind : std::uint8_t {
		/** A learned node of the character (IfAbilityActive): its progression id (the CRC-32 of the lower-case node id). */
		node,
		/** An ability row of a perk of an equipped item (IfAbilityActive External). */
		perk,
		/** The attack input (attack_primary_virtual), the block press (block_press_virtual). */
		attack,
		block,
		/** A collision of the character (character_collided). */
		collided,
		/** The hits of the ability are fewer than `count` (IfAbilityNumHits LessThan). */
		hits,
		/** The fragment with the name `text` plays (IfFragmentPlaying). */
		playing,
	};
	Kind kind{Kind::node};
	std::uint32_t id{0};
	std::string text;
	std::uint32_t count{0};
	bool negated{false};
};

/** @return The input of a name of the data ("virtual", "notHeld", ...); Input::other for an unknown name. */
Input input_of(std::string_view name);
/** @return The progression id of a text (gamedata::progression_id of the backend): the CRC-32 of the text with A to Z in lower
 *  case (an ability id, a node id, a perk ability, a ranged attack name). */
std::uint32_t progression_id(std::string_view text);
/**
 * A literal of the data ([kind, value, negated] of weapon-actions.json: ["input", "attack", false], ["node", NAME, true],
 * ["playing", FRAGMENT, true], ["hits", 1, false]; gamedata read_literal of the backend). @param count The value of "hits".
 * @return False for a kind or an input that the backend does not know.
 */
bool literal_of(std::string_view kind, std::string_view text, std::uint32_t count, bool negated, Literal& out);

struct Transition {
	bool buffered{false};
	/** The window on the clock of the fragment (seconds); `end` below 0: to the end of the fragment. */
	float start{0.0F};
	float end{-1.0F};
	Input input{Input::always};
	/** The next fragment: an index into Graph::fragments, or -1 (a fragment that the graph does not have). */
	int to{-1};
	/** Input::ability: the ability that the press starts (the CRC-32 of the lower-case ability id). */
	std::uint32_t ability{0};
	/** Input::terms: the condition as an OR of ANDs of literals (no term: never; one empty term: always). */
	std::vector<std::vector<Literal>> terms;
};

/** A dodge window of a fragment (CanDodgeCancel: at once; CanDodgeBuffer: at the end of the window). */
struct DodgeWindow {
	float start{0.0F};
	float end{-1.0F};
	bool buffered{false};
};

struct Fragment {
	std::string name;
	/** The end of the animation of the fragment (seconds). 0: not known. */
	float length{0.0F};
	/** The time of the mark CanTransition; below 0: none. */
	float canTransition{-1.0F};
	/** The switch marks: CanUnsheathe_Buffer and the first CanUnsheathe or CanTransition; below 0: none. */
	float switchBuffer{-1.0F};
	float switchAt{-1.0F};
	std::vector<Transition> transitions;
	std::vector<DodgeWindow> dodge;
	/** The named marks of the backend fragment (the throw of Gravity Well: CastGavityWellProjectile at 0.533 s). Below 0: none. */
	struct Marker {
		std::string name;
		float at{-1.0F};
	};
	std::vector<Marker> markers;
	/** @return The time of the named mark, or -1. */
	[[nodiscard]] float marker(std::string_view markName) const;
};

/** The facts of a character for the conditions of the abilities (gamedata::AttackFacts of the backend; stage P3.5). */
struct Facts {
	/** The learned nodes (the progression ids of the nodes of the ProgressionPoint state, slot 67). */
	std::set<std::uint32_t> nodes;
	/** The ability rows of the perks of the equipped items. */
	std::set<std::uint32_t> perks;
	/** A collision of the character at this step (character_collided: the client sees it). */
	bool collided{false};
	/** The hits of the cast that plays (IfAbilityNumHits). */
	std::uint32_t hits{0};
};

/** An ability of a weapon (weapon-actions.json `abilities`; stage P3.5): its id, the fragment of its action, the selections of a
 *  fragment by the learned nodes, and its cooldown (ability-data.json). */
struct Ability {
	std::uint32_t id{0};
	std::string name;
	int fragment{-1};
	/** The nodes and the perk abilities that a selection needs and forbids (their progression ids). */
	struct Selection {
		int fragment{-1};
		std::vector<std::uint32_t> needs;
		std::vector<std::uint32_t> forbids;
		std::vector<std::uint32_t> perks;
		std::vector<std::uint32_t> perkForbids;
	};
	std::vector<Selection> selections;
	/** The cooldown of the data (s; Time of the cooldown row) and its minimum (MinTime). */
	float cooldown{0.0F};
	float minCooldown{0.0F};
	/**
	 * A cast of a projectile spell at a named mark of the fragment (the throw of Gravity Well: CastGavityWellProjectile): the shot
	 * item of the client at that step (protocol notes 4.27, the shot of Gravity Well: the attack is the progression id of the
	 * RangedAttackName of the spell, "GravityWell" 8E14366B), for the nodes of the cast.
	 */
	struct Shot {
		std::string mark;
		std::uint32_t attack{0};
		std::vector<std::uint32_t> needs;
		std::vector<std::uint32_t> forbids;
	};
	std::vector<Shot> shots;
	/** @return The shot of the cast for the learned nodes (the first one whose nodes hold), or null. */
	[[nodiscard]] const Shot* shot_of(const Facts& facts) const;
};

/** The attack graph of one weapon tag set. */
struct Graph {
	/** The first fragment (Attack_Start_Charge). */
	int start{0};
	std::vector<Fragment> fragments;
	std::vector<Ability> abilities;

	/** @return The index of the fragment with this name, or -1. */
	[[nodiscard]] int index_of(std::string_view name) const;
	/** @return The ability with this id, or null. */
	[[nodiscard]] const Ability* ability(std::uint32_t id) const;
	/** @return The fragment of an ability for the learned nodes: the last selection whose nodes hold, else its fragment. */
	[[nodiscard]] int fragment_of(const Ability& ability, const Facts& facts) const;
};

/** The attack bits of one input step. */
struct Bits {
	bool press{false};
	bool hold{false};
	bool release{false};
	/** The dodge input, and whether the stamina can start a dodge now (Stamina::can_dodge; AttackBits::dodgeAllowed). */
	bool dodge{false};
	bool dodgeAllowed{true};
	/** The block press (bit 484): a literal of the transitions inside Charge. */
	bool block{false};
	/** A press at rest can start an attack (AttackBits::attackAllowed of the backend; stage P3.3): false when the bag is over 1.5 x
	 *  its weight limit (Prevent_Attack of the data) or the character is stunned. A chain that plays goes on. */
	bool attackAllowed{true};
};

/**
 * A shot item of an input step: the attack, the row, the charge, and the ammunition of the data (CRC-32 values; stage P3.6), and the
 * origin rule: `forward` m along the look from the character and `up` m above its feet (a basic shot and a ranged ability: 1.0 and
 * 1.46, the live bow item of protocol notes 4.27; the shot of a cast of a melee ability: 0 and 1.5, the Gravity Well of stage P3.5).
 * The direction goes to the aim point of the camera (the session thread has it from the game thread).
 */
struct Shot {
	std::uint32_t attack{0};
	std::uint32_t row{0};
	float charge{0.0F};
	std::uint32_t ammo{0};
	float forward{0.0F};
	float up{1.5F};
};

/** One visit of a fragment. */
struct Visit {
	/** Count of the visits, from 1. */
	std::uint32_t number{0};
	int fragment{0};
	/** The input step of the start: the clock of the fragment is (k - start + 1) / 30 s at the step k. */
	std::uint16_t start{0};
	bool ended{false};
	/** The end came from a change of the weapon (not the end of the animation, not the next fragment). */
	bool interrupted{false};
	std::uint16_t endInput{0};
	/** A buffered transition that took a press: its fragment and the end of its window. */
	int pending{-1};
	float pendingAt{0.0F};
	/** A dodge in a buffered dodge window: the end of the window. Below 0: none. */
	float dodgeAt{-1.0F};
	/** The cast of the ability of the visit (0: a basic attack; the transitions inside an ability keep it). */
	std::uint64_t cast{0};
};

/** What the client did with a press of a key of the bar (stage P3.5). */
enum class PressVerdict : std::uint8_t {
	/** The fragment of the ability starts (at the press, or at the end of a buffered window). */
	played,
	/** No ability on the place of the bar of the weapon in the hand, or no data of it in the graph. */
	empty,
	/** The own cooldown of the client runs: the client does not play the press (it sends its bits). */
	cooldown,
	/** No window of the fragment that plays takes the press (AbilityStart::Verdict::refused). */
	refused,
	/** The limits of the load or of a status (stage P3.3): the bag over its limit, a stun, a silence (the backend refuses the
	 *  press of an encumbered character too: CastVerdict encumbered). */
	limited,
	/** No cast plan of the backend for the ability (stage MVP-A, Combat/SwcBackendSupport.h): the client sends no bit of the key,
	 *  plays nothing, and starts no cooldown. The HUD shows the slot disabled (weaponquickslots.luac GetIsAbilityEnabled). */
	unsupported,
};
inline constexpr std::size_t kPressVerdicts = 6;
[[nodiscard]] const char* press_verdict_text(PressVerdict verdict);

/** The start of an ability (AttackSequences::start_ability of the backend). */
struct AbilityStart {
	enum class Verdict : std::uint8_t { refused, now, later };
	Verdict verdict{Verdict::refused};
	/** The input step of the start, and the steps from the press to it. */
	std::uint16_t at{0};
	unsigned delay{0};
};

/** @return The clock of a fragment that started at `start`, at the input step `input`: (input - start + 1) / 30 s. */
float clock_of(std::uint16_t start, std::uint16_t input);

/** A named mark of a fragment (CAGE-Named of its ProcLayer): a window of its clock in seconds. */
struct Mark {
	std::string name;
	float start{0.0F};
	/** Below 0: to the end of the fragment. */
	float end{-1.0F};
};

/** @return True when a mark with this name holds the clock `t` (IfMannequinMarker). */
bool mark_active(const std::vector<Mark>& marks, std::string_view name, float t);

/**
 * @return The marks whose start is in the clock interval (from, to], in the order of their start (marks at one time: in
 * the order of the data). A fragment that starts gives `from` below 0: a mark at 0 is in it. Each mark comes one time
 * when the intervals of the frames follow one another.
 */
std::vector<const Mark*> marks_between(const std::vector<Mark>& marks, float from, float to);

/** The attack sequence of the character: the visits of the fragments of one graph (AttackSequences::advance). */
class Sequence {
public:
	/** Sets the graph of the weapon in the hand (null: no attack). A different graph ends the sequence: at rest. */
	void set_graph(const Graph* graph);
	[[nodiscard]] const Graph* graph() const { return m_graph; }
	/** Ends the fragment that plays (a change of the weapon). */
	void interrupt(std::uint16_t input);
	/** One input step. */
	void step(std::uint16_t input, const Bits& bits, const Facts& facts = {});
	/**
	 * The press of a key of the bar (stage P3.5; AttackSequences::start_ability of the backend): the fragment of the ability starts
	 * as a visit with the cast. At rest at the press. In a fragment that plays only the step of the press counts: a window of a
	 * transition of the fragment to the ability that holds that step (an instant window at once, a buffered window at its end), or
	 * the press at or after the mark CanTransition or after the end of the fragment. With none, the start is refused.
	 * @param dryRun True: only the verdict.
	 */
	AbilityStart start_ability(std::uint16_t input, std::uint32_t ability, int fragment, std::uint64_t cast, bool dryRun = false);
	/** @return The fragment visit that plays, or null at rest. */
	[[nodiscard]] const Visit* playing() const;
	/** The last visits (the newest at the back; at most 8). */
	[[nodiscard]] const std::deque<Visit>& visits() const { return m_visits; }
	/** @return True one time after a step that permits a dodge (at rest, or a dodge window that ended the fragment). */
	bool take_dodge();

private:
	void start_visit(int fragment, std::uint16_t input, std::uint64_t cast = 0);
	[[nodiscard]] bool holds(const Visit& visit, const Transition& transition, const Bits& bits, bool instant, const Facts& facts) const;

	const Graph* m_graph{nullptr};
	std::deque<Visit> m_visits;
	std::uint32_t m_count{0};
	bool m_dodged{false};
	/** A press of an ability that starts later (a buffered window): its fragment, its cast, and its input step. */
	int m_pendingAbility{-1};
	std::uint64_t m_pendingCast{0};
	std::uint16_t m_pendingInput{0};
};

/** The weapon switch of a weapon key (Combat::select_weapon and Combat::step_switch of the backend). */
class WeaponSwitch {
public:
	struct Pending {
		std::uint32_t slot{0};
		std::uint16_t key{0};
		std::optional<std::uint16_t> at;
		bool pressKept{false};
	};
	/** What one step did. */
	struct Result {
		/** The weapon changed at this step to this slot. */
		std::optional<std::uint32_t> switched;
		/** The key of this step was lost (no switch window of the fragment that plays started in its 20 steps). */
		bool lost{false};
	};

	/** The slot of the weapon in the hand (the active weapon of the paperdoll at the entry, then each switch). */
	void set_held(std::uint32_t slot) { m_held = slot; }
	[[nodiscard]] std::uint32_t held() const { return m_held; }
	/** A weapon key at an input step (slot 24 or 25). The key of the weapon in the hand asks for nothing. */
	void key(std::uint32_t slot, std::uint16_t input);
	/**
	 * One input step before the step of the sequence: the switch at its due step (the sequence ends), and the presses that
	 * the switch keeps (a kept press starts the first attack of the new weapon 3 steps after the switch). Changes `bits`.
	 * @param graphOf The graph of the weapon of a slot (null: no attack graph).
	 */
	template <typename GraphOf>
	Result step(std::uint16_t input, Bits& bits, Sequence& sequence, GraphOf&& graphOf);
	[[nodiscard]] const std::optional<Pending>& pending() const { return m_pending; }

private:
	Result step_inner(std::uint16_t input, Bits& bits, Sequence& sequence, std::optional<std::uint32_t>& change);

	std::uint32_t m_held{0};
	std::optional<Pending> m_pending;
};

template <typename GraphOf>
WeaponSwitch::Result WeaponSwitch::step(std::uint16_t input, Bits& bits, Sequence& sequence, GraphOf&& graphOf)
{
	std::optional<std::uint32_t> change;
	const Result result = step_inner(input, bits, sequence, change);
	if (change) {
		sequence.interrupt(input);
		sequence.set_graph(graphOf(*change));
	}
	return result;
}

/**
 * The attack input of the player at the input steps: the game thread gives the button events and the weapon keys; the
 * thread of the session calls `step` for each input entry, with its number and its action bits (it adds the bits of the
 * attack input). The game thread reads a snapshot. Thread safe.
 */
class Driver {
public:
	/** The state for the game thread. */
	struct Snapshot {
		/** The last input step and the steady time (nanoseconds) at which it went out. */
		std::uint16_t input{0};
		std::int64_t inputNs{0};
		bool stepped{false};
		/** The graph in use, the slot of the weapon in the hand, and the visits (the newest at the back). */
		const Graph* graph{nullptr};
		std::uint32_t held{0};
		std::vector<Visit> visits;
		/** The count of the switches, the last switch (its slot and its step), and the count of the lost keys. */
		std::uint32_t switches{0};
		std::uint32_t switchSlot{0};
		std::uint16_t switchInput{0};
		std::uint32_t lostKeys{0};
		/** The attack input bits of the last step, the counts of the presses, holds, and releases that went out. */
		Bits bits;
		std::uint32_t presses{0};
		std::uint32_t holds{0};
		std::uint32_t releases{0};
		/** The time of the last step on the clock of the steps (s), the predicted stamina, and the dodge (stage P2.5). */
		double time{0.0};
		float stamina{0.0F};
		float staminaMax{0.0F};
		bool exhausted{false};
		float winded{0.0F};
		std::optional<DodgeState> dodge;
		/** The last dodge request: its verdict and its count; the counts of the dodge bits and of the corrections. */
		std::optional<DodgeVerdict> lastVerdict;
		std::uint32_t requests{0};
		std::uint32_t dodgeBits{0};
		std::uint32_t corrections{0};
		std::array<std::uint32_t, kDodgeVerdicts> verdicts{};
		/** The keys of the bar (stage P3.5): the presses that went out, the count of each verdict, and the last press. */
		std::uint32_t abilityPresses{0};
		std::array<std::uint32_t, kPressVerdicts> pressVerdicts{};
		struct AbilityPress {
			unsigned place{0};
			std::uint32_t ability{0};
			PressVerdict verdict{PressVerdict::empty};
			/** The input step of the press and of the start of the fragment, and the cast number of the client. */
			std::uint16_t input{0};
			std::uint16_t at{0};
			std::uint64_t cast{0};
			int fragment{-1};
		};
		std::optional<AbilityPress> lastPress;
		/** The own cooldowns: the end of each ability on the steady clock (ns), and the count of the ends of the server that it took. */
		std::map<std::uint32_t, std::int64_t> cooldowns;
		std::uint32_t cooldownCorrections{0};
		/** The shots of the casts (the throw of Gravity Well), and the steps with the collision of the character. */
		std::uint32_t shots{0};
		std::uint32_t collisions{0};
		/** The dodge presses that the limits of the load or of a status refused (stage P3.3). */
		std::uint32_t limitedDodges{0};
		/**
		 * The ranged actions (stage P3.6, Combat/SwcRanged): the fragment of the scope (its action, its fragment, the record of its
		 * clips, its start step, a serial, its clock), the aim of the data (the tag AimDownSights), the walk, the ground target, the
		 * channel, the turn to the camera, and the counts.
		 */
		struct Ranged {
			bool program{false};
			std::string action;
			std::string fragment;
			std::string record;
			std::uint16_t start{0};
			std::uint32_t serial{0};
			float clock{0.0F};
			bool aimDownSights{false};
			/** The Mannequin tags Aim and ChargedAim of the ranged player (the aim stance of the bow: the half draw and the full draw). */
			bool aim{false};
			bool chargedAim{false};
			bool forceWalk{false};
			std::string targeting;
			std::string channel;
			float facingRate{-1.0F};
			std::uint32_t shots{0};
			std::uint32_t casts{0};
			std::uint32_t abilities{0};
			std::uint32_t payments{0};
			std::uint32_t refusedPayments{0};
			std::uint32_t unknown{0};
			/** The leaf types of the unknown values with their counts ("IfX 3, named:Y 1"). */
			std::string unknownTypes;
			std::uint32_t stuck{0};
			/** ranged::Player::describe() and the facts of the last step (a log line, AST-271). */
			std::string state;
			/** The last shots and casts (the newest at the back; at most 16) for the pictures of the game thread. */
			struct Recent {
				std::uint32_t serial{0};
				bool shot{false};
				std::string spell;
				std::uint32_t attack{0};
				std::uint32_t row{0};
				std::uint16_t input{0};
			};
			std::vector<Recent> recent;
		};
		Ranged ranged;
		/** The predicted mana of the client (ranged::Mana) and whether a mana state of the server came. */
		float mana{0.0F};
		float manaMax{0.0F};
		bool manaKnown{false};
		std::uint32_t manaCorrections{0};
	};

	/** The facts of the character for the conditions of the ranged actions (the game thread, each frame; stage P3.6). */
	struct RangedFacts {
		/** The statuses of the status map (lower-case ids). */
		std::set<std::string> statuses;
		bool stunned{false};
		bool silenced{false};
		bool encumbered{false};
		bool moving{false};
		bool damaged{false};
		bool cameraLock{false};
		bool freeLook{false};
		/** The ground target of the camera ray is in the cast distance of the targeting spell, on a ground. */
		bool groundValid{false};
		float heightToGround{0.0F};
	};

	/** The graph of each weapon slot (24, 25; null: no attack graph) and the weapon in the hand. Resets the sequence. */
	void set_weapons(const Graph* slot1, const Graph* slot2, std::uint32_t held);
	/** The attack button went down or up (the game thread). The steps take one event each, in order. */
	void button(bool down);
	/** The dodge key (the game thread): one step with the dodge bit for each press. */
	void dodge_press();
	/** The aim (the game thread): the aim bit in each step while it is on. */
	void set_aim(bool aim);
	/** The right button (the game thread): the bits of its bindings at its press, while it is down, and at its release (kBitsRight*);
	 *  the press is the block press of the transitions (block_press_virtual). While it is down the aim bit is one of its bits. */
	void set_right_button(bool down);
	/** The ranged program of each weapon slot (24, 25; null: none) and their shared data (stage P3.6). A change resets the player. */
	void set_ranged(const ranged::Program* slot1, const ranged::Program* slot2, const ranged::Shared* shared);
	/** Left Ctrl (the game thread): the self-target bit in each step while it is down. */
	void set_self_target(bool down);
	void set_ranged_facts(const RangedFacts& facts);
	/**
	 * The mana state of the server (slot 47: the value, the base maximum, the delay that is left, the rate in mana/s) at the steady
	 * time of its arrival (ns), and the rate factor of message 4289. The client keeps its own prediction (ranged::Mana).
	 */
	void set_server_mana(float value, float baseMax, float delay, float rate, std::int64_t atNs);
	void set_mana_factor(float factor);
	/** Message 4289 at the steady time of its arrival (ns): the predicted mana takes the values of the server at once (ranged::Mana). */
	void sync_mana(float value, float baseMax, float delay, float factor, std::int64_t atNs);
	/** The stamina data, the equip load class of the character, a world entry (the full stamina). */
	void set_stamina(const StaminaData& data);
	void set_load(LoadClass load);
	void refill();
	/**
	 * The Stamina state of the server (its copy of the component): its value, the delay of the regeneration that was left,
	 * the multiplier of the regeneration, and the steady time of its arrival (ns). The server sends it at a change (a payment,
	 * the full stamina), thus the driver moves it forward with the regeneration. The client keeps its own prediction (protocol
	 * notes 4.38); a difference of more than kCorrection from that forward value for kCorrectionTime with no payment in that
	 * time takes it (a rule of this project: the retail client takes the server copy at a rewind of its movement).
	 */
	void set_server_stamina(float value, float regenDelay, float regenMultiplier, std::int64_t atNs);
	static constexpr float kCorrection = 10.0F;
	static constexpr double kCorrectionTime = 2.0;
	/** The bar of each weapon slot (24, 25): the ability ids of the places 0 to 2 (the Ability state, slot 46; 0: none). */
	void set_bars(const std::array<std::uint32_t, 3>& slot1, const std::array<std::uint32_t, 3>& slot2);
	/** The learned nodes of the character (the ProgressionPoint state, slot 67) and the ability rows of the perks of its items. */
	void set_facts(std::set<std::uint32_t> nodes, std::set<std::uint32_t> perks);
	/**
	 * The abilities that the backend plays (stage MVP-A; Combat/SwcBackendSupport.h supported_abilities). A press of a key whose ability
	 * is not in the set sends no bit, plays nothing, and starts no cooldown (PressVerdict::unsupported). An empty set: no limit (the
	 * parity tests of the attack graph).
	 */
	void set_supported_abilities(std::set<std::uint32_t> abilities);
	/** The hit counts of the tracked abilities (the ability state, slot 46, fields 1 and 2): the condition IfAbilityNumHits of the cast
	 *  that plays reads the count of its ability. */
	void set_ability_hits(std::map<std::uint32_t, std::uint8_t> hits);
	/** The character collided in a frame of the game thread (character_collided): the next step sees it. */
	void collided();
	/** A key of the bar went down or up (the place 0 to 2; the game thread). The steps take one event of each key in each step. */
	void ability_key(unsigned place, bool down);
	/**
	 * The cooldowns of the server (the CooldownTimers state, slot 45): the ability ids with their end on the steady clock (ns).
	 * The client starts its own cooldown at a press that it plays, with the time of the data (protocol notes 4.22: the setter
	 * runs at the press; the client does not play a press in its own cooldown, and its HUD shows the countdown of the server
	 * state). The own cooldown only blocks a press. The end of an entry of the server is the end of the client (a rule of this
	 * project: the backend keeps the end of the client equal to its end, also after a reduction and with the cooldown factor of
	 * the character, world/abilities.cpp AbilityCooldown::clientEnd); an ability with no entry keeps its own end (a reset of
	 * the server: the client keeps its cooldown, live check I8-05). An indefinite end (all ones) changes nothing. Each end of
	 * an entry applies one time (a state that comes again changes nothing).
	 * @param endsUs The ability ids with their ends on the server clock (us); `serverNowUs` and `nowNs` give the steady clock.
	 */
	void set_server_cooldowns(const std::vector<std::pair<std::uint32_t, std::uint64_t>>& endsUs, std::uint64_t serverNowUs, std::int64_t nowNs);
	/**
	 * The limits of the load and of the statuses for the actions (stage P3.3, swc::move::Limits): a dodge, a press of the bar, and an
	 * attack from rest that the client does not play. The bits of the press go out (the retail client sends the dodge bit of a
	 * press that it refuses, protocol notes 4.38).
	 */
	void set_action_limits(bool dodgeAllowed, bool abilitiesAllowed, bool attackAllowed);
	/**
	 * Called for each input entry (the session thread): adds the attack bits and the bits of the keys of the bar to `actionBits`,
	 * steps the sequence, and then takes a press of a key of the bar (the order of the backend: RepSession::handle_attacks steps
	 * the sequence, then presses the ability). `shots`: the attack of each shot of a cast at this step (the mark of its spell).
	 */
	void step(std::uint16_t input, std::int64_t nowNs, std::vector<unsigned>& actionBits, std::vector<Shot>* shots = nullptr);
	[[nodiscard]] Snapshot snapshot() const;

private:
	[[nodiscard]] const Graph* graph_of(std::uint32_t slot) const;
	/** The ranged step of the weapon in the hand (stage P3.6). `pressed`: the place of the key of the bar that this step pressed (-1). */
	void step_ranged(std::uint16_t input, std::int64_t nowNs, const Bits& bits, int pressed, bool weaponKey, std::vector<Shot>* shots);

	mutable std::mutex m_lock;
	const Graph* m_graphs[2]{nullptr, nullptr};
	std::deque<bool> m_events;
	bool m_down{false};
	unsigned m_dodgePresses{0};
	bool m_aim{false};
	bool m_right{false};
	bool m_rightWas{false};
	Sequence m_sequence;
	WeaponSwitch m_switch;
	Stamina m_stamina;
	bool m_stepped{false};
	bool m_refill{true};
	std::uint16_t m_lastInput{0};
	double m_time{0.0};
	struct ServerCopy {
		float value{0.0F};
		float delay{0.0F};
		std::int64_t atNs{0};
	};
	std::optional<ServerCopy> m_server;
	double m_differentSince{-1.0};
	std::uint32_t m_paymentsSeen{0};
	double m_lastPayment{-100.0};
	/** The keys of the bar: the events of each key, its state, and the bar of each weapon slot. */
	std::array<std::deque<bool>, 3> m_keyEvents;
	std::array<bool, 3> m_keyDown{};
	std::set<std::uint32_t> m_supported;
	std::map<std::uint32_t, std::uint8_t> m_abilityHits;
	std::array<std::array<std::uint32_t, 3>, 2> m_bars{};
	Facts m_facts;
	bool m_collided{false};
	std::map<std::uint32_t, std::int64_t> m_cooldowns;
	std::map<std::uint32_t, std::uint64_t> m_serverEnds;
	bool m_dodgeAllowed{true};
	bool m_abilitiesAllowed{true};
	bool m_attackAllowed{true};
	std::uint64_t m_casts{0};
	/** The cast that plays its fragments: its number, its ability, and whether its shot went out. */
	std::uint64_t m_cast{0};
	std::uint32_t m_castAbility{0};
	bool m_shotSent{false};
	/** The ranged actions (stage P3.6): the program of each slot, the player of the program in the hand, the keys, the facts, the mana. */
	const ranged::Program* m_programs[2]{nullptr, nullptr};
	const ranged::Shared* m_rangedShared{nullptr};
	const ranged::Program* m_rangedNow{nullptr};
	ranged::Player m_ranged;
	bool m_selfTarget{false};
	bool m_aimWas{false};
	RangedFacts m_rangedFacts;
	ranged::Mana m_mana;
	struct ServerMana {
		float value{0.0F};
		float baseMax{0.0F};
		float delay{0.0F};
		float rate{0.0F};
		std::int64_t atNs{0};
	};
	std::optional<ServerMana> m_serverMana;
	std::optional<ServerMana> m_manaSync;
	std::optional<float> m_manaFactor;
	std::uint32_t m_recentSerial{0};
	Snapshot m_snapshot;
};

} // namespace swc::combat
