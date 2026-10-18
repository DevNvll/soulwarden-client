#include "Combat/SwcStamina.h"

#include <algorithm>
#include <cmath>

namespace swc::combat {

LoadClass load_class_of(float load, float maximum, float ratioMedium, float ratioHeavy, float ratioOverburdened)
{
	const float percent = maximum > 0.0F ? load / maximum * 100.0F : 0.0F;
	return percent < ratioMedium ? LoadClass::light
		: percent < ratioHeavy   ? LoadClass::medium
		: percent <= ratioOverburdened ? LoadClass::heavy
									   : LoadClass::overburdened;
}

const char* dodge_verdict_text(DodgeVerdict verdict)
{
	switch (verdict) {
	case DodgeVerdict::accepted:
		return "accepted";
	case DodgeVerdict::buffered:
		return "buffered";
	case DodgeVerdict::dodging:
		return "a dodge plays";
	case DodgeVerdict::noData:
		return "no stamina data";
	case DodgeVerdict::dead:
		return "dead";
	case DodgeVerdict::overburdened:
		return "overburdened";
	case DodgeVerdict::exhausted:
		return "exhausted";
	case DodgeVerdict::noStamina:
		return "no stamina";
	}
	return "unknown";
}

void Stamina::set_data(const StaminaData& data)
{
	m_data = data;
	m_value = std::min(m_value, m_data.max);
}

void Stamina::refill(double now)
{
	m_value = m_data.max;
	m_regenFrom = now;
	m_heldUntil = now;
	m_windedUntil = now;
	m_last = now;
	m_exhausted = false;
	m_dodge.reset();
}

void Stamina::change(double now, float amount)
{
	if (amount == 0.0F || !std::isfinite(amount)) {
		return;
	}
	const bool wasFull = m_value >= m_data.max;
	m_value = std::clamp(m_value + amount, 0.0F, m_data.max);
	if (amount < 0.0F) {
		// A payment sets the delay of the regeneration (StaminaRegenDelay, or StaminaRegenDelayOnEmpty at 0); no debt.
		const bool empty = m_value <= 0.0F;
		m_regenFrom = now + (empty ? m_data.regenDelayOnEmpty : m_data.regenDelay);
		++m_payments;
		if (empty && !m_exhausted) {
			// At 0: winded for m_maxWindedTime, and exhausted until the stamina is full.
			m_exhausted = true;
			m_windedUntil = now + m_data.maxWindedTime;
		}
	}
	if (m_value >= m_data.max) {
		m_exhausted = false;
	} else if (wasFull) {
		// The regeneration starts to look at the character (Stamina::watch).
		m_last = now;
	}
}

void Stamina::pay(double now, float cost)
{
	if (cost > 0.0F) {
		change(now, -cost * m_data.costMult[static_cast<std::size_t>(m_load)]);
	}
}

void Stamina::correct(double now, float value)
{
	m_value = std::clamp(value, 0.0F, m_data.max);
	m_last = now;
	if (m_value >= m_data.max) {
		m_exhausted = false;
	}
}

void Stamina::regenerate(double now)
{
	if (m_value >= m_data.max) {
		m_exhausted = false;
		m_last = now;
		return;
	}
	const double from = std::max({m_last, m_regenFrom, m_heldUntil});
	if (now <= from) {
		return;
	}
	const float rate = this->rate();
	m_last = now;
	m_value = std::min(m_value + rate * static_cast<float>(now - from), m_data.max);
	if (m_value >= m_data.max) {
		m_exhausted = false;
	}
}

void Stamina::advance(double now)
{
	if (m_dodge) {
		DodgeState& state = *m_dodge;
		if (!state.paid && now >= state.payAt) {
			// PayStaminaCost at its mark (0.1 s); the delay of the payment counts from the mark.
			state.paid = true;
			change(state.payAt, -state.cost);
		}
		if (!state.transitioned && now >= state.transitionAt) {
			// CanTransition: the regeneration is on again with the delay of the exit of the action (1 s).
			state.transitioned = true;
			if (m_value < m_data.max) {
				m_regenFrom = state.transitionAt + m_data.dodgeExitDelay;
			}
		}
		if (state.next && now >= *state.next) {
			const double at = *state.next;
			state.next.reset();
			const DodgeVerdict checks = start_checks();
			if (checks == DodgeVerdict::accepted) {
				start(at);
			} else {
				++m_verdicts[static_cast<std::size_t>(checks)];
			}
		}
	}
	regenerate(now);
}

DodgeVerdict Stamina::start_checks() const
{
	if (!m_data.loaded) {
		return DodgeVerdict::noData;
	}
	if (m_load == LoadClass::overburdened) {
		return DodgeVerdict::overburdened;
	}
	if (m_exhausted) {
		return DodgeVerdict::exhausted;
	}
	if (!(m_value > 0.0F)) {
		return DodgeVerdict::noStamina;
	}
	return DodgeVerdict::accepted;
}

bool Stamina::can_dodge() const
{
	const DodgeVerdict verdict = start_checks();
	return verdict == DodgeVerdict::accepted || verdict == DodgeVerdict::noData;
}

DodgeVerdict Stamina::dodge(double now)
{
	const auto counted = [this](DodgeVerdict verdict) {
		++m_verdicts[static_cast<std::size_t>(verdict)];
		return verdict;
	};
	const DodgeFragment* fragment = m_dodge && m_data.dodges[static_cast<std::size_t>(m_dodge->load)]
		? &*m_data.dodges[static_cast<std::size_t>(m_dodge->load)] : nullptr;
	if (fragment != nullptr) {
		DodgeState& playing = *m_dodge;
		const float t = static_cast<float>(now - playing.start);
		if (playing.next) {
			// A second press in the buffered window: the next dodge waits already (one for each window).
			return counted(DodgeVerdict::buffered);
		}
		const DodgeFragment::Window* window = nullptr;
		for (const DodgeFragment::Window& cancel : fragment->cancel) {
			if (t >= cancel.start && (!cancel.end || t < *cancel.end)) {
				window = &cancel;
				break;
			}
		}
		if (window == nullptr && t < fragment->canTransition) {
			return counted(DodgeVerdict::dodging);
		}
		if (window != nullptr && window->buffered) {
			const DodgeVerdict checks = start_checks();
			if (checks != DodgeVerdict::accepted) {
				return counted(checks);
			}
			// CAGE-TransitionBuffered: the next dodge starts at the end of the window.
			playing.next = playing.start + *window->end;
			return counted(DodgeVerdict::buffered);
		}
	}
	const DodgeVerdict checks = start_checks();
	if (checks != DodgeVerdict::accepted) {
		return counted(checks);
	}
	start(now);
	return counted(DodgeVerdict::accepted);
}

void Stamina::start(double at)
{
	if (m_load == LoadClass::overburdened || !m_data.dodges[static_cast<std::size_t>(m_load)]) {
		return;
	}
	const DodgeFragment& fragment = *m_data.dodges[static_cast<std::size_t>(m_load)];
	// A payment that is due and not taken yet is not lost when a next dodge replaces the dodge that plays.
	if (m_dodge && !m_dodge->paid && at >= m_dodge->payAt) {
		m_dodge->paid = true;
		change(at, -m_dodge->cost);
	}
	DodgeState state;
	state.number = ++m_count;
	state.load = m_load;
	state.start = at;
	state.payAt = at + fragment.pay;
	state.transitionAt = at + fragment.canTransition;
	state.cost = m_data.dodgeCosts[static_cast<std::size_t>(m_load)] * m_data.costMult[static_cast<std::size_t>(m_load)];
	// DisableStaminaRegen of the action Dodge: no regeneration from the start to CanTransition.
	m_heldUntil = std::max(m_heldUntil, state.transitionAt);
	m_dodge = state;
}

} // namespace swc::combat
