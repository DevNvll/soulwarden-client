#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
/** The weapon keys: Use_MainHandSlot_1 and quickslot-weapon1 (slot 24), Use_MainHandSlot_2 and quickslot-weapon2 (slot 25). */
inline constexpr unsigned kBitsKey1[2] = {9, 40};
inline constexpr unsigned kBitsKey2[2] = {355, 306};
inline constexpr std::uint32_t kMainHand1 = 24;

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
	/** The start of an ability, a condition of an ability: not a step of a basic attack. */
	other,
};

/** @return The input of a name of the data ("virtual", "notHeld", ...); Input::other for an unknown name. */
Input input_of(std::string_view name);

struct Transition {
	bool buffered{false};
	/** The window on the clock of the fragment (seconds); `end` below 0: to the end of the fragment. */
	float start{0.0F};
	float end{-1.0F};
	Input input{Input::always};
	/** The next fragment: an index into Graph::fragments, or -1 (a fragment that the graph does not have). */
	int to{-1};
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
};

/** The attack graph of one weapon tag set. */
struct Graph {
	/** The first fragment (Attack_Start_Charge). */
	int start{0};
	std::vector<Fragment> fragments;

	/** @return The index of the fragment with this name, or -1. */
	[[nodiscard]] int index_of(std::string_view name) const;
};

/** The attack bits of one input step. */
struct Bits {
	bool press{false};
	bool hold{false};
	bool release{false};
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
	void step(std::uint16_t input, const Bits& bits);
	/** @return The fragment visit that plays, or null at rest. */
	[[nodiscard]] const Visit* playing() const;
	/** The last visits (the newest at the back; at most 8). */
	[[nodiscard]] const std::deque<Visit>& visits() const { return m_visits; }

private:
	void start_visit(int fragment, std::uint16_t input);

	const Graph* m_graph{nullptr};
	std::deque<Visit> m_visits;
	std::uint32_t m_count{0};
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
	};

	/** The graph of each weapon slot (24, 25; null: no attack graph) and the weapon in the hand. Resets the sequence. */
	void set_weapons(const Graph* slot1, const Graph* slot2, std::uint32_t held);
	/** The attack button went down or up (the game thread). The steps take one event each, in order. */
	void button(bool down);
	/** Called for each input entry (the session thread): adds the attack bits to `actionBits` and steps the sequence. */
	void step(std::uint16_t input, std::int64_t nowNs, std::vector<unsigned>& actionBits);
	[[nodiscard]] Snapshot snapshot() const;

private:
	[[nodiscard]] const Graph* graph_of(std::uint32_t slot) const;

	mutable std::mutex m_lock;
	const Graph* m_graphs[2]{nullptr, nullptr};
	std::deque<bool> m_events;
	bool m_down{false};
	Sequence m_sequence;
	WeaponSwitch m_switch;
	Snapshot m_snapshot;
};

} // namespace swc::combat
