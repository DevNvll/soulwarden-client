#include "Combat/SwcAttackGraph.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace swc::combat {
namespace {

/** @return The steps from one input number to a later one, or a value above 0x7FFF for an earlier one (16 bits, with wrap). */
unsigned steps_between(std::uint16_t from, std::uint16_t to)
{
	return static_cast<std::uint16_t>(to - from);
}

bool in_window(float t, float start, float end)
{
	return t >= start - kClockEpsilon && (end < 0.0F || t < end - kClockEpsilon);
}

bool takes_press(Input input)
{
	return input == Input::virtualInput || input == Input::press;
}

/** @return True when the input of a step fits the input of a transition (an instant one, or a buffered window that takes a press). */
bool fits(Input wanted, const Bits& bits, bool instant)
{
	switch (wanted) {
	case Input::always:
		return true;
	case Input::virtualInput:
		// The hold counts at an instant transition (the repeat of a long hold), not in a buffered window.
		return bits.press || (instant && bits.hold);
	case Input::press:
		return bits.press;
	case Input::notHeld:
		return !bits.press && !bits.hold;
	case Input::released:
		// A step with neither the attack input nor its hold is a release too: the partial heavy attack, not a full charge.
		return bits.release || (!bits.press && !bits.hold);
	case Input::ability:
	case Input::terms:
	case Input::other:
		// The start of an ability is a press (start_ability); the terms are a condition of the data (Sequence::holds).
		return false;
	}
	return false;
}

/** @return True for a fragment of a charge: it has a transition at the release (Attack_Start_Charge of each weapon). */
bool charges(const Fragment& fragment)
{
	return std::any_of(fragment.transitions.begin(), fragment.transitions.end(),
		[](const Transition& transition) { return transition.input == Input::released; });
}

} // namespace

Input input_of(std::string_view name)
{
	if (name == "always") {
		return Input::always;
	}
	if (name == "virtual") {
		return Input::virtualInput;
	}
	if (name == "press") {
		return Input::press;
	}
	if (name == "notHeld") {
		return Input::notHeld;
	}
	if (name == "released") {
		return Input::released;
	}
	if (name == "ability") {
		return Input::ability;
	}
	if (name == "terms") {
		return Input::terms;
	}
	return Input::other;
}

std::uint32_t progression_id(std::string_view text)
{
	// The CRC-32 of the IEEE polynomial (zlib crc32) of the text with A to Z in lower case (gamedata::item_id_crc of the backend).
	std::uint32_t crc = 0xFFFFFFFFU;
	for (const char c : text) {
		const auto byte = static_cast<std::uint8_t>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
		crc ^= byte;
		for (int bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
		}
	}
	return ~crc;
}

bool literal_of(std::string_view kind, std::string_view text, std::uint32_t count, bool negated, Literal& out)
{
	out = Literal{};
	out.negated = negated;
	if (kind == "hits") {
		out.kind = Literal::Kind::hits;
		out.count = count;
		return true;
	}
	if (text.empty()) {
		return false;
	}
	out.text = std::string(text);
	if (kind == "node" || kind == "perk") {
		out.kind = kind == "node" ? Literal::Kind::node : Literal::Kind::perk;
		out.id = progression_id(text);
		return true;
	}
	if (kind == "input") {
		if (text == "attack") {
			out.kind = Literal::Kind::attack;
		} else if (text == "block") {
			out.kind = Literal::Kind::block;
		} else if (text == "collided") {
			out.kind = Literal::Kind::collided;
		} else {
			return false;
		}
		return true;
	}
	if (kind == "playing") {
		out.kind = Literal::Kind::playing;
		return true;
	}
	return false;
}

const char* press_verdict_text(PressVerdict verdict)
{
	switch (verdict) {
	case PressVerdict::played:
		return "played";
	case PressVerdict::empty:
		return "no ability";
	case PressVerdict::cooldown:
		return "own cooldown";
	case PressVerdict::refused:
		return "no window";
	}
	return "?";
}

float Fragment::marker(std::string_view markName) const
{
	for (const Marker& item : markers) {
		if (item.name == markName) {
			return item.at;
		}
	}
	return -1.0F;
}

const Ability::Shot* Ability::shot_of(const Facts& facts) const
{
	for (const Shot& shot : shots) {
		const bool needs = std::all_of(shot.needs.begin(), shot.needs.end(), [&](std::uint32_t node) { return facts.nodes.count(node) > 0; });
		const bool forbids = std::none_of(shot.forbids.begin(), shot.forbids.end(), [&](std::uint32_t node) { return facts.nodes.count(node) > 0; });
		if (needs && forbids) {
			return &shot;
		}
	}
	return nullptr;
}

const Ability* Graph::ability(std::uint32_t id) const
{
	for (const Ability& item : abilities) {
		if (item.id == id) {
			return &item;
		}
	}
	return nullptr;
}

int Graph::fragment_of(const Ability& ability, const Facts& facts) const
{
	// The last selection whose nodes and perks hold (ChangeActionFragment; Abilities::press_entity of the backend), else the
	// fragment of the action.
	const auto all = [](const std::vector<std::uint32_t>& ids, const std::set<std::uint32_t>& set) {
		return std::all_of(ids.begin(), ids.end(), [&](std::uint32_t id) { return set.count(id) > 0; });
	};
	const auto none = [](const std::vector<std::uint32_t>& ids, const std::set<std::uint32_t>& set) {
		return std::none_of(ids.begin(), ids.end(), [&](std::uint32_t id) { return set.count(id) > 0; });
	};
	int fragment = ability.fragment;
	for (const Ability::Selection& selection : ability.selections) {
		if (all(selection.needs, facts.nodes) && none(selection.forbids, facts.nodes) && all(selection.perks, facts.perks)
			&& none(selection.perkForbids, facts.perks) && selection.fragment >= 0) {
			fragment = selection.fragment;
		}
	}
	return fragment;
}

int Graph::index_of(std::string_view name) const
{
	for (std::size_t index = 0; index < fragments.size(); ++index) {
		if (fragments[index].name == name) {
			return static_cast<int>(index);
		}
	}
	return -1;
}

float clock_of(std::uint16_t start, std::uint16_t input)
{
	return static_cast<float>(steps_between(start, input) + 1) / kStepsInSecond;
}

bool mark_active(const std::vector<Mark>& marks, std::string_view name, float t)
{
	return std::any_of(marks.begin(), marks.end(), [&](const Mark& mark) { return mark.name == name && in_window(t, mark.start, mark.end); });
}

std::vector<const Mark*> marks_between(const std::vector<Mark>& marks, float from, float to)
{
	std::vector<const Mark*> out;
	for (const Mark& mark : marks) {
		if (mark.start > from && mark.start <= to) {
			out.push_back(&mark);
		}
	}
	std::stable_sort(out.begin(), out.end(), [](const Mark* a, const Mark* b) { return a->start < b->start; });
	return out;
}

void Sequence::set_graph(const Graph* graph)
{
	m_graph = graph;
}

void Sequence::interrupt(std::uint16_t input)
{
	m_pendingAbility = -1;
	m_pendingCast = 0;
	if (!m_visits.empty() && !m_visits.back().ended) {
		m_visits.back().ended = true;
		m_visits.back().interrupted = true;
		m_visits.back().endInput = input;
	}
}

bool Sequence::take_dodge()
{
	const bool dodged = m_dodged;
	m_dodged = false;
	return dodged;
}

const Visit* Sequence::playing() const
{
	return !m_visits.empty() && !m_visits.back().ended ? &m_visits.back() : nullptr;
}

void Sequence::start_visit(int fragment, std::uint16_t input, std::uint64_t cast)
{
	if (!m_visits.empty() && !m_visits.back().ended) {
		m_visits.back().ended = true;
		m_visits.back().endInput = input;
	}
	// The fragments of one ability keep its cast (a transition inside the ability: the exit of Whirlwind, the uppercut of Charge).
	if (cast == 0 && !m_visits.empty() && m_visits.back().cast != 0 && m_graph != nullptr && fragment >= 0
		&& fragment < static_cast<int>(m_graph->fragments.size()) && m_graph->fragments[static_cast<std::size_t>(fragment)].name.rfind("Attack_", 0) != 0) {
		cast = m_visits.back().cast;
	}
	Visit visit;
	visit.number = ++m_count;
	visit.fragment = fragment;
	visit.start = input;
	visit.cast = cast;
	m_visits.push_back(visit);
	while (m_visits.size() > 8) {
		m_visits.pop_front();
	}
}

bool Sequence::holds(const Visit& visit, const Transition& transition, const Bits& bits, bool instant, const Facts& facts) const
{
	if (transition.input != Input::terms) {
		return fits(transition.input, bits, instant);
	}
	// The terms of a transition inside an ability (AttackSequences::holds of the backend). The client sees its collision (the
	// server sees it only through a contact).
	for (const std::vector<Literal>& term : transition.terms) {
		bool all = true;
		for (const Literal& literal : term) {
			bool value = false;
			switch (literal.kind) {
			case Literal::Kind::node:
				value = facts.nodes.count(literal.id) > 0;
				break;
			case Literal::Kind::perk:
				value = facts.perks.count(literal.id) > 0;
				break;
			case Literal::Kind::attack:
				value = bits.press || (instant && bits.hold);
				break;
			case Literal::Kind::block:
				value = bits.block;
				break;
			case Literal::Kind::collided:
				value = facts.collided;
				break;
			case Literal::Kind::hits:
				value = facts.hits < literal.count;
				break;
			case Literal::Kind::playing:
				value = visit.fragment >= 0 && m_graph->fragments[static_cast<std::size_t>(visit.fragment)].name == literal.text;
				break;
			}
			if (value == literal.negated) {
				all = false;
				break;
			}
		}
		if (all) {
			return true;
		}
	}
	return false;
}

AbilityStart Sequence::start_ability(std::uint16_t input, std::uint32_t ability, int fragment, std::uint64_t cast, bool dryRun)
{
	AbilityStart result;
	result.at = input;
	if (m_graph == nullptr || fragment < 0 || fragment >= static_cast<int>(m_graph->fragments.size())) {
		return result;
	}
	const Visit* visit = playing();
	std::optional<std::uint16_t> at;
	if (visit == nullptr) {
		at = input;
	} else {
		// Only the step of the press counts: an instant window of the ability that holds it starts the ability at the press, a
		// buffered window at its end; the mark CanTransition and the end of the fragment at the press (user test 2, UT2-1).
		const Fragment& data = m_graph->fragments[static_cast<std::size_t>(visit->fragment)];
		const float t = clock_of(visit->start, input);
		const auto consider = [&](std::uint16_t step) {
			if (!at || steps_between(step, *at) <= 0x7FFF) {
				at = step;
			}
		};
		for (const Transition& transition : data.transitions) {
			if (transition.input != Input::ability || transition.ability != ability || !in_window(t, transition.start, transition.end)) {
				continue;
			}
			if (transition.buffered && transition.end >= 0.0F) {
				const long steps = std::max(std::lround(transition.end * kStepsInSecond) - 1L, 0L);
				consider(static_cast<std::uint16_t>(visit->start + static_cast<unsigned>(steps)));
			} else {
				consider(input);
			}
		}
		if (data.canTransition >= 0.0F && t >= data.canTransition - kClockEpsilon) {
			consider(input);
		}
		if (data.length > 0.0F && t > data.length + kClockEpsilon) {
			consider(input);
		}
	}
	if (!at) {
		return result;
	}
	result.at = *at;
	result.delay = steps_between(input, *at);
	result.verdict = result.delay == 0 ? AbilityStart::Verdict::now : AbilityStart::Verdict::later;
	if (dryRun) {
		return result;
	}
	if (result.delay == 0) {
		m_pendingAbility = -1;
		m_pendingCast = 0;
		// The ability ends the fragment that plays (as a transition of it).
		start_visit(fragment, input, cast);
		return result;
	}
	m_pendingAbility = fragment;
	m_pendingCast = cast;
	m_pendingInput = *at;
	return result;
}

void Sequence::step(std::uint16_t input, const Bits& bits, const Facts& facts)
{
	if (m_graph == nullptr || m_graph->fragments.empty()) {
		return;
	}
	const Graph& graph = *m_graph;
	// A press starts one new attack at most (from rest, or in the tail of a fragment).
	bool pressUsed = false;
	for (int loop = 0; loop < kTransitionsInStep; ++loop) {
		// A press of an ability in a buffered window: its fragment starts at the end of the window.
		if (m_pendingAbility >= 0 && steps_between(m_pendingInput, input) <= 0x7FFF) {
			const int fragment = m_pendingAbility;
			const std::uint64_t cast = m_pendingCast;
			m_pendingAbility = -1;
			m_pendingCast = 0;
			start_visit(fragment, m_pendingInput, cast);
			continue;
		}
		Visit* visit = !m_visits.empty() && !m_visits.back().ended ? &m_visits.back() : nullptr;
		if (visit == nullptr) {
			// A dodge input at rest: no fragment of an attack decides it.
			if (bits.dodge) {
				m_dodged = true;
			}
			if (!bits.press || pressUsed) {
				return;
			}
			// From rest: the first fragment of the graph.
			pressUsed = true;
			start_visit(graph.start, input);
			continue;
		}
		if (visit->fragment < 0 || visit->fragment >= static_cast<int>(graph.fragments.size())) {
			visit->ended = true;
			visit->endInput = input;
			return;
		}
		const Fragment& fragment = graph.fragments[static_cast<std::size_t>(visit->fragment)];
		const float t = clock_of(visit->start, input);
		// The end of the animation of the fragment: at rest (a press of this step starts a new attack).
		if (fragment.length > 0.0F && t > fragment.length + kClockEpsilon && visit->pending < 0) {
			visit->ended = true;
			visit->endInput = input;
			continue;
		}
		// A dodge: at once in a cancel window, at the end of a buffered window; the stamina must be able to start it (a refused
		// dodge leaves the attack as it is).
		if (visit->dodgeAt >= 0.0F && t >= visit->dodgeAt - kClockEpsilon) {
			if (bits.dodgeAllowed) {
				interrupt(input);
				m_dodged = true;
				return;
			}
			visit->dodgeAt = -1.0F;
		}
		if (bits.dodge && bits.dodgeAllowed) {
			bool ends = false;
			for (const DodgeWindow& window : fragment.dodge) {
				if (!in_window(t, window.start, window.end)) {
					continue;
				}
				// A cancel window that starts with the fragment is no end (the client plays no dodge there).
				if (!window.buffered && window.start <= kClockEpsilon) {
					break;
				}
				if (!window.buffered || window.end < 0.0F) {
					ends = true;
					break;
				}
				if (visit->dodgeAt < 0.0F) {
					visit->dodgeAt = window.end;
				}
				break;
			}
			if (ends) {
				interrupt(input);
				m_dodged = true;
				return;
			}
		}
		// A buffered transition that took a press: its fragment starts at the end of its window, at its due input step.
		if (visit->pending >= 0 && t >= visit->pendingAt - kClockEpsilon) {
			const long dueSteps = std::max(std::lround(visit->pendingAt * kStepsInSecond) - 1L, 0L);
			const unsigned atSteps = std::min(static_cast<unsigned>(dueSteps), steps_between(visit->start, input));
			start_visit(visit->pending, static_cast<std::uint16_t>(visit->start + atSteps));
			continue;
		}
		const Transition* taken = nullptr;
		bool laterWindow = false;
		for (const Transition& transition : fragment.transitions) {
			// The start of an ability is a press of its key (start_ability), not an input of a step.
			if (transition.input == Input::ability || transition.input == Input::other) {
				continue;
			}
			if (takes_press(transition.input) && (transition.end < 0.0F || transition.end > t + kClockEpsilon)) {
				laterWindow = true;
			}
			if (!in_window(t, transition.start, transition.end)) {
				continue;
			}
			// The full charge needs the hold at its step: a charge with no hold at its end is no full charge.
			if (transition.input == Input::always && charges(fragment) && !bits.press && !bits.hold) {
				continue;
			}
			if (!transition.buffered && holds(*visit, transition, bits, true, facts)) {
				taken = &transition;
				break;
			}
			if (transition.buffered && visit->pending < 0 && holds(*visit, transition, bits, false, facts)) {
				visit->pending = transition.to;
				visit->pendingAt = transition.end;
			}
		}
		if (taken != nullptr) {
			if (taken->to < 0) {
				visit->ended = true;
				visit->endInput = input;
				return;
			}
			pressUsed = pressUsed || takes_press(taken->input);
			start_visit(taken->to, input);
			continue;
		}
		// A press that no window of the fragment takes, now or later: the tail of the fragment; a new attack from rest.
		if (bits.press && !pressUsed && !laterWindow && visit->pending < 0) {
			visit->ended = true;
			visit->endInput = input;
			continue;
		}
		return;
	}
}

void WeaponSwitch::key(std::uint32_t slot, std::uint16_t input)
{
	// A key of the weapon in the hand, or of the weapon that a switch already changed to, asks for nothing.
	if ((m_held == slot && !m_pending) || (m_pending && m_pending->slot == slot)) {
		return;
	}
	// The switch started: no key in its first 3 steps.
	if (m_pending && m_pending->at) {
		return;
	}
	Pending request;
	request.slot = slot;
	request.key = input;
	request.pressKept = m_pending && m_pending->pressKept;
	m_pending = request;
}

WeaponSwitch::Result WeaponSwitch::step_inner(std::uint16_t input, Bits& bits, Sequence& sequence, std::optional<std::uint32_t>& change)
{
	Result result;
	if (!m_pending) {
		return result;
	}
	Pending& pending = *m_pending;
	const unsigned sinceKey = steps_between(pending.key, input);
	if (sinceKey > 0x7FFF) {
		return result;
	}
	if (!pending.at) {
		// The step of the switch: at rest at the key; in a fragment at its switch mark when a switch window started in the
		// 20 steps of the key (grid rule 62 Attack -> Sheathe).
		const Visit* visit = sequence.playing();
		const Graph* graph = sequence.graph();
		std::optional<std::uint16_t> due;
		if (visit == nullptr || graph == nullptr || visit->fragment < 0 || visit->fragment >= static_cast<int>(graph->fragments.size())) {
			if (sinceKey <= kKeySteps) {
				due = input;
			}
		} else {
			const Fragment& fragment = graph->fragments[static_cast<std::size_t>(visit->fragment)];
			if (fragment.switchAt >= 0.0F) {
				// The step whose clock reaches the mark.
				const std::uint16_t start = visit->start;
				const auto step_of = [start](float time) {
					return static_cast<std::uint16_t>(start + static_cast<std::uint16_t>(std::max(std::ceil(time * kStepsInSecond - 0.001F), 1.0F)) - 1);
				};
				const std::uint16_t condition = step_of(fragment.switchAt);
				const std::uint16_t buffer = fragment.switchBuffer >= 0.0F ? step_of(fragment.switchBuffer) : condition;
				const std::uint16_t key = pending.key;
				const auto after_key = [key](std::uint16_t step) { return static_cast<std::uint16_t>(step - key); };
				const std::uint16_t opens = after_key(buffer) <= 0x7FFF ? buffer : key;
				if (after_key(opens) <= kKeySteps || after_key(condition) > 0x7FFF || after_key(condition) <= kKeySteps) {
					due = after_key(condition) <= 0x7FFF ? condition : key;
				}
			}
		}
		if (!due) {
			if (sinceKey > kKeySteps) {
				// No window started in the 20 steps of the key: the key is lost, the weapon stays.
				m_pending.reset();
				result.lost = true;
				return result;
			}
		} else if (steps_between(*due, input) <= 0x7FFF) {
			pending.at = input;
			m_held = pending.slot;
			change = pending.slot;
			result.switched = pending.slot;
		}
	}
	// Until 3 steps after the switch the presses start nothing and are kept (grid rule 116).
	const unsigned sinceSwitch = pending.at ? steps_between(*pending.at, input) : 0U;
	if (pending.at && (pending.pressKept || bits.press) && !bits.hold && sinceSwitch + 1 >= kFirstAttackSteps) {
		// A kept press with the button up: the light swing of the new weapon starts 3 steps after the switch.
		bits.press = true;
		m_pending.reset();
		return result;
	}
	if (!pending.at || sinceSwitch < kFirstAttackSteps) {
		if (bits.press) {
			pending.pressKept = true;
			bits.press = false;
		}
		return result;
	}
	// A kept press with the button down: the charge of the new weapon starts 3 steps after the switch.
	if (pending.pressKept && !bits.press) {
		bits.press = true;
	}
	m_pending.reset();
	return result;
}

const Graph* Driver::graph_of(std::uint32_t slot) const
{
	return slot == kMainHand1 ? m_graphs[0] : slot == kMainHand1 + 1 ? m_graphs[1] : nullptr;
}

void Driver::set_weapons(const Graph* slot1, const Graph* slot2, std::uint32_t held)
{
	const std::lock_guard<std::mutex> guard(m_lock);
	m_graphs[0] = slot1;
	m_graphs[1] = slot2;
	m_switch = WeaponSwitch();
	m_switch.set_held(held);
	m_sequence.interrupt(m_snapshot.input);
	m_sequence.set_graph(graph_of(held));
	m_snapshot.graph = m_sequence.graph();
	m_snapshot.held = held;
	m_cast = 0;
	m_castAbility = 0;
}

void Driver::set_bars(const std::array<std::uint32_t, 3>& slot1, const std::array<std::uint32_t, 3>& slot2)
{
	const std::lock_guard<std::mutex> guard(m_lock);
	m_bars[0] = slot1;
	m_bars[1] = slot2;
}

void Driver::set_facts(std::set<std::uint32_t> nodes, std::set<std::uint32_t> perks)
{
	const std::lock_guard<std::mutex> guard(m_lock);
	m_facts.nodes = std::move(nodes);
	m_facts.perks = std::move(perks);
}

void Driver::collided()
{
	const std::lock_guard<std::mutex> guard(m_lock);
	m_collided = true;
}

void Driver::ability_key(unsigned place, bool down)
{
	if (place >= m_keyEvents.size()) {
		return;
	}
	const std::lock_guard<std::mutex> guard(m_lock);
	std::deque<bool>& events = m_keyEvents[place];
	events.push_back(down);
	while (events.size() > 16) {
		events.pop_front();
	}
}

void Driver::set_server_cooldowns(const std::vector<std::pair<std::uint32_t, std::uint64_t>>& endsUs, std::uint64_t serverNowUs, std::int64_t nowNs)
{
	const std::lock_guard<std::mutex> guard(m_lock);
	for (const auto& [ability, endUs] : endsUs) {
		if (endUs == std::numeric_limits<std::uint64_t>::max()) {
			continue; // An indefinite cooldown of the server: the client has no cooldown of its own for it (the Pylon).
		}
		// Each end of the server one time: a state that comes again with the same entry changes nothing.
		std::uint64_t& seen = m_serverEnds[ability];
		if (seen == endUs) {
			continue;
		}
		seen = endUs;
		const std::int64_t end = nowNs + (static_cast<std::int64_t>(endUs) - static_cast<std::int64_t>(serverNowUs)) * 1000;
		m_cooldowns[ability] = end;
		++m_snapshot.cooldownCorrections;
	}
}

void Driver::dodge_press()
{
	const std::lock_guard<std::mutex> guard(m_lock);
	m_dodgePresses = std::min(m_dodgePresses + 1, 8U);
}

void Driver::set_aim(bool aim)
{
	const std::lock_guard<std::mutex> guard(m_lock);
	m_aim = aim;
}

void Driver::set_stamina(const StaminaData& data)
{
	const std::lock_guard<std::mutex> guard(m_lock);
	m_stamina.set_data(data);
	m_refill = true;
}

void Driver::set_load(LoadClass load)
{
	const std::lock_guard<std::mutex> guard(m_lock);
	m_stamina.set_load(load);
}

void Driver::refill()
{
	const std::lock_guard<std::mutex> guard(m_lock);
	m_refill = true;
}

void Driver::set_server_stamina(float value, float regenDelay, float regenMultiplier, std::int64_t atNs)
{
	const std::lock_guard<std::mutex> guard(m_lock);
	if (std::isfinite(value) && std::isfinite(regenDelay)) {
		m_server = ServerCopy{value, std::max(regenDelay, 0.0F), atNs};
	}
	if (std::isfinite(regenMultiplier) && regenMultiplier > 0.0F) {
		m_stamina.set_rate_multiplier(regenMultiplier);
	}
}

void Driver::button(bool down)
{
	const std::lock_guard<std::mutex> guard(m_lock);
	m_events.push_back(down);
	while (m_events.size() > 16) {
		m_events.pop_front();
	}
}

void Driver::step(std::uint16_t input, std::int64_t nowNs, std::vector<unsigned>& actionBits, std::vector<std::uint32_t>* shots)
{
	const std::lock_guard<std::mutex> guard(m_lock);
	// The button: one event in each step (a click in one frame of the game is a press, then a release at the next step).
	Bits bits;
	if (!m_events.empty()) {
		const bool down = m_events.front();
		m_events.pop_front();
		if (down && !m_down) {
			bits.press = true;
		} else if (!down && m_down) {
			bits.release = true;
		}
		m_down = down;
	}
	// The hold: each step while the button is down, from the step after the press.
	bits.hold = m_down && !bits.press;
	if (m_aim) {
		actionBits.push_back(kBitAim);
	}
	// The dodge key: one press for each step.
	if (m_dodgePresses > 0) {
		--m_dodgePresses;
		bits.dodge = true;
		actionBits.push_back(kBitDodge);
	}
	// The clock of the steps (s): 1/30 s for each input number.
	m_time += m_stepped ? static_cast<double>(steps_between(m_lastInput, input)) / kStepsInSecond : 0.0;
	m_lastInput = input;
	m_stepped = true;
	if (m_refill) {
		m_refill = false;
		m_stamina.refill(m_time);
	}
	m_stamina.advance(m_time);
	// The correction from the server copy (a rule of this project, set_server_stamina).
	if (m_stamina.payments() != m_paymentsSeen) {
		m_paymentsSeen = m_stamina.payments();
		m_lastPayment = m_time;
	}
	// The server copy moved forward to now: its regeneration after the delay that was left.
	std::optional<float> server;
	if (m_server) {
		const double since = static_cast<double>(nowNs - m_server->atNs) / 1e9;
		server = std::min(m_stamina.maximum(), m_server->value + m_stamina.rate() * static_cast<float>(std::max(since - m_server->delay, 0.0)));
	}
	if (server && std::fabs(*server - m_stamina.value()) > kCorrection && m_time - m_lastPayment > kCorrectionTime) {
		if (m_differentSince < 0.0) {
			m_differentSince = m_time;
		} else if (m_time - m_differentSince >= kCorrectionTime) {
			m_stamina.correct(m_time, *server);
			m_differentSince = -1.0;
			++m_snapshot.corrections;
		}
	} else {
		m_differentSince = -1.0;
	}
	bits.dodgeAllowed = m_stamina.can_dodge();
	if (bits.press) {
		actionBits.insert(actionBits.end(), {kBitPress, kBitPressVirtual, kBitShoot});
	}
	if (bits.hold) {
		actionBits.push_back(kBitHold);
	}
	if (bits.release) {
		actionBits.push_back(kBitRelease);
	}
	// The keys of the bar: one event of each key in each step; the first entry of a press has the press bit and the virtual bit,
	// each step while the key is down the hold bit, the step of the release the release bit. One press in each step (the backend
	// reads one ability key in an entry); the press of a second key waits for the next step.
	int pressed = -1;
	for (unsigned place = 0; place < m_keyEvents.size(); ++place) {
		std::deque<bool>& events = m_keyEvents[place];
		bool press = false;
		bool release = false;
		if (!events.empty() && !(events.front() && !m_keyDown[place] && pressed >= 0)) {
			const bool down = events.front();
			events.pop_front();
			press = down && !m_keyDown[place];
			release = !down && m_keyDown[place];
			m_keyDown[place] = down;
		}
		if (press) {
			pressed = static_cast<int>(place);
			actionBits.insert(actionBits.end(), {kBitsAbilityPress[place], kBitsAbilityVirtual[place]});
		} else if (m_keyDown[place]) {
			actionBits.push_back(kBitsAbilityHold[place]);
		}
		if (release) {
			actionBits.push_back(kBitsAbilityRelease[place]);
		}
	}
	// The facts of the conditions inside an ability: the collision of the character since the last step (the client sees it).
	Facts facts = m_facts;
	facts.collided = m_collided;
	m_collided = false;
	// The weapon keys of this entry (the game thread puts their bits into one entry).
	const auto has = [&actionBits](unsigned bit) { return std::find(actionBits.begin(), actionBits.end(), bit) != actionBits.end(); };
	if (has(kBitsKey1[0]) || has(kBitsKey1[1])) {
		m_switch.key(kMainHand1, input);
	} else if (has(kBitsKey2[0]) || has(kBitsKey2[1])) {
		m_switch.key(kMainHand1 + 1, input);
	}
	Bits checked = bits;
	const WeaponSwitch::Result result = m_switch.step(input, checked, m_sequence, [this](std::uint32_t slot) { return graph_of(slot); });
	m_sequence.step(input, checked, facts);
	// The press of a key of the bar after the step of the sequence (RepSession::handle_attacks: the attack step, then the press).
	std::optional<Snapshot::AbilityPress> press;
	if (pressed >= 0) {
		Snapshot::AbilityPress made;
		made.place = static_cast<unsigned>(pressed);
		made.input = input;
		const std::uint32_t held = m_switch.held();
		const int bar = held == kMainHand1 ? 0 : held == kMainHand1 + 1 ? 1 : -1;
		made.ability = bar >= 0 ? m_bars[static_cast<std::size_t>(bar)][made.place] : 0;
		const Graph* graph = m_sequence.graph();
		const Ability* ability = graph != nullptr && made.ability != 0 ? graph->ability(made.ability) : nullptr;
		const auto running = m_cooldowns.find(made.ability);
		if (ability == nullptr) {
			made.verdict = PressVerdict::empty;
		} else if (running != m_cooldowns.end() && nowNs < running->second) {
			made.verdict = PressVerdict::cooldown;
		} else {
			made.fragment = graph->fragment_of(*ability, facts);
			const AbilityStart start = m_sequence.start_ability(input, made.ability, made.fragment, m_casts + 1);
			if (start.verdict == AbilityStart::Verdict::refused) {
				made.verdict = PressVerdict::refused;
			} else {
				made.verdict = PressVerdict::played;
				made.cast = ++m_casts;
				made.at = start.at;
				// The own cooldown from the press, with the time of the data (the setter of the client runs at the press).
				m_cooldowns[made.ability] = nowNs + static_cast<std::int64_t>(static_cast<double>(ability->cooldown) * 1e9);
				m_cast = made.cast;
				m_castAbility = made.ability;
				m_shotSent = false;
			}
		}
		press = made;
	}
	// The shot of the cast that plays: at the step of the named mark of its spell in its fragment (the throw of Gravity Well).
	if (const Visit* visit = m_sequence.playing(); visit != nullptr && m_cast != 0 && visit->cast == m_cast && !m_shotSent) {
		const Graph* graph = m_sequence.graph();
		const Ability* ability = graph->ability(m_castAbility);
		const Ability::Shot* shot = ability != nullptr ? ability->shot_of(facts) : nullptr;
		const float at = shot != nullptr ? graph->fragments[static_cast<std::size_t>(visit->fragment)].marker(shot->mark) : -1.0F;
		if (at >= 0.0F && clock_of(visit->start, input) >= at - kClockEpsilon) {
			m_shotSent = true;
			++m_snapshot.shots;
			if (shots != nullptr) {
				shots->push_back(shot->attack);
			}
		}
	}
	// The dodge (Combat::step): the sequence permits it at rest or in a dodge window; with no attack graph each dodge input.
	std::optional<DodgeVerdict> verdict;
	if (m_sequence.take_dodge() || (bits.dodge && m_sequence.graph() == nullptr)) {
		verdict = m_stamina.dodge(m_time);
	}

	Snapshot& out = m_snapshot;
	out.input = input;
	out.inputNs = nowNs;
	out.stepped = true;
	out.graph = m_sequence.graph();
	out.held = m_switch.held();
	out.visits.assign(m_sequence.visits().begin(), m_sequence.visits().end());
	if (result.switched) {
		++out.switches;
		out.switchSlot = *result.switched;
		out.switchInput = input;
	}
	out.lostKeys += result.lost ? 1 : 0;
	out.time = m_time;
	out.stamina = m_stamina.value();
	out.staminaMax = m_stamina.maximum();
	out.exhausted = m_stamina.exhausted();
	out.winded = m_stamina.winded(m_time);
	out.dodge = m_stamina.dodge_state();
	if (verdict) {
		out.lastVerdict = verdict;
		++out.requests;
	}
	out.dodgeBits += bits.dodge ? 1 : 0;
	out.verdicts = m_stamina.verdicts();
	out.bits = bits;
	out.presses += bits.press ? 1 : 0;
	out.holds += bits.hold ? 1 : 0;
	out.releases += bits.release ? 1 : 0;
	if (press) {
		++out.abilityPresses;
		++out.pressVerdicts[static_cast<std::size_t>(press->verdict)];
		out.lastPress = press;
	}
	out.collisions += facts.collided ? 1 : 0;
	// The own cooldowns that run (an end that passed leaves).
	for (auto it = m_cooldowns.begin(); it != m_cooldowns.end();) {
		it = it->second <= nowNs ? m_cooldowns.erase(it) : std::next(it);
	}
	out.cooldowns = m_cooldowns;
}

Driver::Snapshot Driver::snapshot() const
{
	const std::lock_guard<std::mutex> guard(m_lock);
	return m_snapshot;
}

} // namespace swc::combat
