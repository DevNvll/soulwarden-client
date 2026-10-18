#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// The ranged and magic actions of the player on the client (stage P3.6, docs/RANGED.md): the bow, the fire staff, and the life
// staff. The backend has no attack graph for these weapons: it takes a basic shot from the shot item of list 2 and the attack bits
// of its press (Soulwarden protocol notes 4.27; backend world/projectiles.cpp Projectiles::launch). Thus the client plays the action
// lists and the animation databases of the data (actions.json "ranged", tools/convert-player.sh actions): the activation and the
// deactivation conditions of the actions, the transitions of their fragments, the shot, cast, and payment items, the activities of
// the actions with the conditions of their branches, and the named conditions of the action lists. Plain C++ (no engine type):
// tools/test-ranged.sh tests it with the data and the input reader of the backend.

namespace swc::ranged {

/** Input steps in one second (the simulation of the client). */
inline constexpr float kStepsInSecond = 30.0F;
/** A tolerance of the clock comparisons (the value of the attack graph, attack_rules::kClockEpsilon of the backend). */
inline constexpr float kClockEpsilon = 0.0005F;
/** The most transitions in one step (the value of the attack graph, attack_rules::kTransitionsInStep). */
inline constexpr int kTransitionsInStep = 6;
/** The steps of the input history: the largest BackTicks of the data is 20. */
inline constexpr unsigned kHistorySteps = 32;
/**
 * Rules of this project: an action whose fragment is done and whose deactivation condition stays false for this time ends (a leaf
 * of the condition that the client does not know must not hold the scope), and an installed action that lost the fragment scope
 * ends after kLostTime (its tags must not stay).
 */
inline constexpr float kStuckTime = 2.0F;
inline constexpr float kLostTime = 10.0F;

/** A condition tree of the data: [type, attrs, children] of actions.json (the IfThenElse conditions, the activations, the named conditions). */
struct Node {
	std::string type;
	std::map<std::string, std::string> attrs;
	std::vector<Node> children;
	/** @return The value of an attribute, or an empty text. */
	[[nodiscard]] std::string_view attr(std::string_view key) const;
};

enum class ItemKind : std::uint8_t { transitionInstant, transitionBuffered, rangedAttack, castSpell, named, payMana, damage, other };

/** A timed procedural item of a fragment: [type, start, end, params] (seconds; `end` below 0: to the end of the fragment). */
struct Item {
	ItemKind kind{ItemKind::other};
	float start{0.0F};
	float end{-1.0F};
	std::map<std::string, std::string> params;
	[[nodiscard]] std::string_view param(std::string_view key) const;
};

/** One variant of a fragment of the SubADB chain: its Mannequin tags (lower case, beyond the weapon tags), its record for the clips. */
struct Variant {
	std::set<std::string> tags;
	std::string record;
	/** The end of the animation (seconds; 0: not known), and the loop flag of its last clip. */
	float length{0.0F};
	bool loop{false};
	std::vector<Item> items;
};

struct Fragment {
	std::string name;
	std::vector<Variant> variants;
	/** @return The variant with the most tags in `tags` (the rule of find_fragment of the converter), or null. */
	[[nodiscard]] const Variant* variant_for(const std::set<std::string>& tags) const;
};

/** An activity of an action: its type, the attributes of its Default, and the condition of its IfThenElse branches (none: the enter). */
struct Activity {
	std::string type;
	std::map<std::string, std::string> attrs;
	std::optional<Node> guard;
	[[nodiscard]] std::string_view attr(std::string_view key) const;
};

struct Action {
	std::string name;
	/** The fragment of the action (empty: an action with no fragment, which runs beside the fragment action). */
	std::string fragment;
	std::optional<Node> activation;
	std::optional<Node> deactivation;
	std::vector<Activity> activities;
};

/** An ability of the ability table of the weapon (the activation IfActiveAbilityMoveName of its start actions). */
struct Ability {
	std::string id;
	/** The CRC-32 of the lower-case id (the id of the bar, the Ability state of slot 46). */
	std::uint32_t crc{0};
	struct Start {
		std::string action;
		/** The start needs the self-target key (Left Ctrl). */
		bool self{false};
	};
	std::vector<Start> starts;
	/** The cooldown of the data (s; Time and MinTime of the cooldown row; below 0: none), and the CostID of IfManaCost with its value. */
	float cooldown{-1.0F};
	float minCooldown{-1.0F};
	std::string costId;
	float manaCost{0.0F};
};

/** The flight of the own projectile picture (projectile-data.json of the backend copy: the slice and the attack of the shot rule). */
struct Flight {
	bool known{false};
	float speed{0.0F};
	std::array<float, 3> accel{};
	float life{0.0F};
	float accelDelay{0.0F};
	float maxRange{0.0F};
	float radius{0.0F};
	float nearRange{0.0F};
};

/** A shot rule of a profile (projectile-data.json "shots"): the attack, the row, the charge, the spell, the heavy flag, the flight. */
struct ShotRule {
	std::string profile;
	std::string attack;
	std::uint32_t attackCrc{0};
	std::string row;
	std::uint32_t rowCrc{0};
	float charge{0.0F};
	std::string spell;
	bool heavy{false};
	bool ability{false};
	Flight flight;
};

/** A spell of the spell tables (the values of the client rules). */
struct Spell {
	std::string id;
	std::string types;
	std::string attack;
	std::uint32_t attackCrc{0};
	float castDistance{0.0F};
	float castHeight{0.0F};
	float radius{0.0F};
	float height{0.0F};
	float duration{0.0F};
	bool cameraLock{false};
	std::string manaCost;
	[[nodiscard]] bool projectile() const { return types.find("Projectile") != std::string::npos && attackCrc != 0; }
};

/** The data that the three weapons share (actions.json "ranged"). */
struct Shared {
	std::vector<ShotRule> shots;
	/** The spells by lower-case id, the mana costs by lower-case CostID, and the profile and the ammunition of each weapon item. */
	std::map<std::string, Spell> spells;
	std::map<std::string, float> manaCosts;
	struct Weapon {
		std::string profile;
		std::string ammo;
	};
	std::map<std::string, Weapon> weapons;
	[[nodiscard]] const Spell* spell(std::string_view id) const;
	/** @return The mana cost of a CostID (0: none). */
	[[nodiscard]] float mana_cost(std::string_view costId) const;
	/** @return The rule of a shot (its attack and row; for a spell its spell), or null. */
	[[nodiscard]] const ShotRule* rule(std::uint32_t attackCrc, std::uint32_t rowCrc, std::string_view spell) const;
};

/** The program of one weapon tag set (the "ranged" entry of its weapon in actions.json). */
struct Program {
	/** The weapon tags (lower case: "bow", "longbow"). */
	std::set<std::string> weaponTags;
	/** The fragments and the actions by lower-case name (Mannequin compares the names with no case). */
	std::map<std::string, Fragment> fragments;
	std::map<std::string, Action> actions;
	std::vector<Ability> abilities;
	/** The named conditions of the action lists (one name space, as the engine loads all lists into one controller). */
	std::map<std::string, Node> conditions;
	/** The actions where the basic attacks start (the aim and the hip shot of the bow, the charge of a staff). */
	std::vector<std::string> start;
	/** The actions in the order of the data (the activations are read in this order). */
	std::vector<std::string> order;
	/** The ammunition key of the shot items of the weapon (DefaultArrow for a bow; empty for a magic weapon). */
	std::string ammo;
	[[nodiscard]] const Fragment* fragment(std::string_view name) const;
	[[nodiscard]] const Action* action(std::string_view name) const;
	[[nodiscard]] const Ability* ability(std::uint32_t crc) const;
	[[nodiscard]] const Ability* ability_named(std::string_view id) const;
	[[nodiscard]] const Node* condition(std::string_view name) const;
};

/** The inputs of one step (the game thread gives the key events; the player keeps their history for BackTicks). */
struct Input {
	/** The left mouse button: down in the step, its press step, its release step. */
	bool attackDown{false};
	bool attackPress{false};
	bool attackRelease{false};
	/** The right mouse button (aim_draw and block are on it in the default profile). */
	bool aimDown{false};
	bool aimPress{false};
	bool aimRelease{false};
	/** The keys of the bar (Q, R, F): their press step and their state. */
	std::array<bool, 3> keyPress{};
	std::array<bool, 3> keyDown{};
	/** Left Ctrl (self_target), the dodge press, a weapon key (Use_MainHandSlot_1, _2, quickslot-weaponSwap). */
	bool selfTarget{false};
	bool dodge{false};
	bool weaponKey{false};
};

/** The facts of the character in a step for the conditions. */
struct Facts {
	/** The predicted mana of the client. */
	float mana{100.0F};
	/** The learned nodes (progression ids) and the statuses of the status map (lower-case status ids). */
	std::set<std::uint32_t> nodes;
	std::set<std::string> statuses;
	bool stunned{false};
	bool silenced{false};
	bool encumbered{false};
	/** The character took damage in this step (IfDamagedByPowerLevel: the stagger of Flamethrower, the end of a healing aura). */
	bool damaged{false};
	/** The character is moving (no tag Idle) and its height above the ground (m). */
	bool moving{false};
	float heightToGround{0.0F};
	bool cameraLock{false};
	bool freeLook{false};
	/** The abilities of the places of the bar of the weapon in the hand (their CRC-32; 0: none). */
	std::array<std::uint32_t, 3> bar{};
	/** The own cooldowns that run (ability CRC-32 values). */
	std::set<std::uint32_t> cooldowns;
	/** The ground target of the camera ray is valid (in the cast distance of the targeting spell, on a ground). */
	bool groundValid{false};
	/** The player settings that hold (IfPlayerSetting). */
	std::set<std::string> settings{"AttackFacing"};
};

/** What a step of the player did: the shots, the casts, the payments, the cooldowns, the starts of the abilities. */
struct Event {
	enum class Kind : std::uint8_t {
		/** A shot item: attack, row, charge, ammunition (CRC-32 values); `text` the spell of a magic shot. */
		shot,
		/** A cast of a spell (`text`); a projectile spell also gives a shot. */
		cast,
		/** A mana payment (`amount`, `text` the CostID). */
		mana,
		/** The own cooldown of an ability starts (`ability`, `text` its id). */
		cooldown,
		/** An ability action started (`ability`). */
		ability,
		/** The raycast of a ground target (`text` the spell; `passed`). */
		raycast,
		/** A CAGE-Damage item of the client (the server makes the damage; for the counts). */
		damage,
	};
	Kind kind{Kind::shot};
	std::string text;
	std::uint32_t attack{0};
	std::uint32_t row{0};
	float charge{0.0F};
	std::uint32_t ammo{0};
	std::uint32_t ability{0};
	float amount{0.0F};
	bool passed{false};
	/** The input step of the event. */
	std::uint16_t input{0};
};

/** The state of the fragment action for the game thread. */
struct Playing {
	const Action* action{nullptr};
	const Fragment* fragment{nullptr};
	const Variant* variant{nullptr};
	/** The input step of the start of the fragment (its clock: (step - start + 1) / 30 s). */
	std::uint16_t start{0};
	/** A new number for each start of a fragment (the animation starts it again). */
	std::uint32_t serial{0};
	float clock{0.0F};
};

/**
 * The mana of the client (stage P3.6): the regeneration of the data (javelindata_basevitals_player, row Player: ManaBaseMax 100,
 * ManaBaseRegenRate 2 a second, ManaRegenDelay 0 s, ManaRegenDelayOnEmpty 2 s) times the rate factor of the server (message 4289,
 * the 1 + ManaRate parts), and the payments of the actions. The mana state of the server (slot 47: the current value, the base
 * maximum, the delay that is left, the rate) comes at each change of the server: a difference of more than kCorrection from its
 * forward value for kCorrectionTime with no payment in that time takes it (the rule of the stamina, Driver::set_server_stamina).
 */
class Mana {
public:
	static constexpr float kBaseMax = 100.0F;
	static constexpr float kBaseRate = 2.0F;
	static constexpr float kDelay = 0.0F;
	static constexpr float kDelayOnEmpty = 2.0F;
	static constexpr float kCorrection = 5.0F;
	static constexpr double kCorrectionTime = 2.0;

	/** The regeneration to `time` (s, the clock of the steps). */
	void advance(double time);
	/** A payment at `time`. @return False when the mana is too low (nothing is paid). */
	bool pay(double time, float amount);
	/** The mana state of the server at `time`: its value, its base maximum, the delay that is left (s), the rate (mana/s). */
	void set_server(double time, float value, float baseMax, float delay, float rate);
	/** The rate factor of message 4289 (1 + the ManaRate parts). */
	void set_factor(float factor) { m_factor = factor; }
	/**
	 * Message 4289 (ManaComponentClientFacet_ClientForceSyncServerState) at `time`: the retail client copies the four values of the
	 * server into its predicted mana (backend mana_messages.h: the handler 0x656DE10), thus the value is taken at once.
	 */
	void force_sync(double time, float value, float baseMax, float delay, float factor);
	[[nodiscard]] float value() const { return m_value; }
	[[nodiscard]] float maximum() const { return m_max; }
	[[nodiscard]] bool known() const { return m_known; }
	[[nodiscard]] std::uint32_t payments() const { return m_payments; }
	[[nodiscard]] std::uint32_t corrections() const { return m_corrections; }

private:
	float m_value{kBaseMax};
	float m_max{kBaseMax};
	float m_factor{1.0F};
	double m_time{0.0};
	double m_regenFrom{0.0};
	double m_lastPayment{-100.0};
	double m_differentSince{-1.0};
	bool m_known{false};
	std::uint32_t m_payments{0};
	std::uint32_t m_corrections{0};
	struct Server {
		double time{0.0};
		float value{0.0F};
		float delay{0.0F};
		float rate{0.0F};
	};
	std::optional<Server> m_server;
};

/**
 * The player of the actions of one weapon. The installed actions run their activities; one of them owns the fragment scope (the upper
 * body): the last action that started a fragment. An action that loses the scope to a new action stays installed to its deactivation
 * condition (Bow_ReadyWeapon to the mark Exit_BowAimShoot of the lower fragment: its tags hold the variants of the next actions); an
 * action with no deactivation condition ends with its fragment or when it loses the scope. Each step: the history of the inputs, the
 * deactivation of the installed actions, the items and the transitions of the fragment at its clock, the end of a fragment, the
 * activities at the edges of their conditions, and the activation of the actions that are not installed (in the order of the data;
 * the enter activities of a new action run at its start).
 */
class Player {
public:
	void set_program(const Program* program, const Shared* shared);
	[[nodiscard]] const Program* program() const { return m_program; }
	/** Ends each action (a weapon switch, a death, a travel): no event. */
	void reset();
	/** One input step. */
	void step(std::uint16_t input, const Input& in, const Facts& facts, std::vector<Event>& out);

	[[nodiscard]] const Playing& playing() const { return m_playing; }
	[[nodiscard]] const std::set<std::string>& tags() const { return m_tags; }
	/** @return True while a named mark of the fragment is on. */
	[[nodiscard]] bool marker(std::string_view name) const;
	/** The aim of the bow (the tag AimDownSights: the camera AimCamera and the walk), the walk of a fragment (the mark ForceWalk). */
	[[nodiscard]] bool aim_down_sights() const;
	[[nodiscard]] bool force_walk() const;
	/** The spell of the ground target while CastSpellTargeting is on (empty: none), and the channel spell while ChannelSpell is on. */
	[[nodiscard]] const std::string& targeting() const { return m_targeting; }
	[[nodiscard]] const std::string& channel() const { return m_channel; }
	/** The turn rate to the camera while a CameraRelativeFacing activity is on (below 0: none). */
	[[nodiscard]] float facing_rate() const { return m_facingRate; }
	/** The counts of the leaves of the conditions that the client does not know (type -> count). */
	[[nodiscard]] const std::map<std::string, std::uint32_t>& unknown() const { return m_unknown; }
	/** The count of the actions that ended by kStuckTime. */
	[[nodiscard]] std::uint32_t stuck() const { return m_stuckEnds; }

	/**
	 * @return The value of a condition tree in the state of the last step. `host`: the action whose condition it is (IfFragmentPlaying
	 *         and IfFragmentDone with UseHostActionFragment read the fragment of that action).
	 */
	[[nodiscard]] bool holds(const Node& node, const Facts& facts, const Action* host = nullptr) const;

private:
	struct Active {
		const Action* action{nullptr};
		/** The guard value of each activity at the last step (the edges), and the steps since a delayed tag waits (-1: none). */
		std::vector<bool> on;
		std::vector<int> delay;
		/** The fragment of a fragment action is done since this step (-1: not done), and it lost the scope since this step (-1: not). */
		int doneSince{-1};
		int lostSince{-1};
	};
	[[nodiscard]] bool leaf(const Node& node, const Facts& facts, const Action* host) const;
	[[nodiscard]] bool has_input(std::string_view name, unsigned backTicks) const;
	[[nodiscard]] bool named(std::string_view name, const Facts& facts, const Action* host) const;
	void start_action(const Action& action, std::uint16_t input, const Facts& facts, std::vector<Event>& out);
	void end_action(std::size_t index, std::uint16_t input, const Facts& facts, std::vector<Event>& out);
	void start_fragment(std::string_view name, std::uint16_t input);
	void run_items(std::uint16_t input, const Facts& facts, std::vector<Event>& out);
	bool run_transitions(std::uint16_t input, const Facts& facts, std::vector<Event>& out);
	void run_activities(Active& active, bool entering, bool exiting, std::uint16_t input, const Facts& facts, std::vector<Event>& out);
	void apply(const Activity& activity, bool rising, std::uint16_t input, const Facts& facts, std::vector<Event>& out);
	void cast(std::string_view spell, std::uint16_t input, std::vector<Event>& out);
	void update_flags();

	const Program* m_program{nullptr};
	const Shared* m_shared{nullptr};
	std::deque<Input> m_history;
	std::uint16_t m_input{0};
	std::vector<Active> m_active;
	Playing m_playing;
	std::uint32_t m_serial{0};
	std::set<std::string> m_tags;
	/** The fired items of the fragment (their indexes) and the buffered windows that took their condition. */
	std::vector<bool> m_fired;
	std::vector<bool> m_buffered;
	/** The ability inputs of the bar: the ability of each press with its step (cleared by ClearActiveAbilityMoveInput). */
	std::map<std::uint32_t, std::uint16_t> m_abilityInputs;
	/** The ability of the fragment action that an ability start began (0: a basic action). */
	std::uint32_t m_ability{0};
	/** The raycast of the ground target: 0 none, 1 passed, 2 failed. */
	int m_raycast{0};
	std::string m_targeting;
	std::string m_channel;
	float m_facingRate{-1.0F};
	bool m_forceWalk{false};
	mutable std::map<std::string, std::uint32_t> m_unknown;
	std::uint32_t m_stuckEnds{0};
};

} // namespace swc::ranged
