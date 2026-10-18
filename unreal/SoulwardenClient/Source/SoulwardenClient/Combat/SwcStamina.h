#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The stamina of the player and the dodge on the client (stage P2.5, docs/ACTIONS.md): the prediction of the retail client
// (protocol notes 4.38: the client predicts its stamina; the Stamina state of the server does not change the predicted value)
// with the rules of the backend (world/stamina.cpp: Stamina::change, regenerate, start_checks, dodge, start, step_dodge) at the
// input steps of the client. Plain C++ (no engine type): tools/test-actions.sh tests it.

namespace swc::combat {

/** The equip load class (the sum of the loads of the armor against the encumbrance row of the player). */
enum class LoadClass : std::uint8_t { light, medium, heavy, overburdened };
inline constexpr std::size_t kLoadClasses = 4;

/**
 * @return The equip load class of a load (the sum of the whole loads of the armor items, tenths) against the encumbrance row
 * of the player (world/gear.cpp of the backend): below ratioMedium percent of the maximum light, below ratioHeavy medium, to
 * ratioOverburdened heavy, else overburdened.
 */
LoadClass load_class_of(float load, float maximum, float ratioMedium, float ratioHeavy, float ratioOverburdened);

/** The verdict of a dodge request (DodgeVerdict of the backend). */
enum class DodgeVerdict : std::uint8_t { accepted, buffered, dodging, noData, dead, overburdened, exhausted, noStamina };
inline constexpr std::size_t kDodgeVerdicts = 8;
const char* dodge_verdict_text(DodgeVerdict verdict);

/** A dodge fragment of a load class: its marks (seconds) and its cancel windows (stamina-data.json of the backend copy). */
struct DodgeFragment {
	std::string name;
	float pay{0.1F};
	float immuneEnd{0.5F};
	float canTransition{0.5F};
	float attackCancel{0.0F};
	struct Window {
		float start{0.0F};
		std::optional<float> end;
		bool buffered{false};
	};
	std::vector<Window> cancel;
};

struct StaminaData {
	float min{0.0F};
	float max{100.0F};
	float regenRate{33.33F};
	float maxWindedTime{2.0F};
	float regenDelay{1.0F};
	float regenDelayOnEmpty{1.0F};
	float dodgeExitDelay{1.0F};
	std::array<float, kLoadClasses> costMult{1.0F, 1.0F, 1.0F, 1.0F};
	std::array<float, kLoadClasses> regenMult{1.0F, 1.0F, 1.0F, 1.0F};
	/** The dodge of each load class (light, medium, heavy; overburdened has none): its fragment and its cost. */
	std::array<std::optional<DodgeFragment>, 3> dodges;
	std::array<float, 3> dodgeCosts{45.0F, 45.0F, 50.0F};
	bool loaded{false};
};

/** The dodge that plays. */
struct DodgeState {
	/** Count of the dodges, from 1 (the animation starts a fragment for each one). */
	std::uint32_t number{0};
	LoadClass load{LoadClass::light};
	double start{0.0};
	double payAt{0.0};
	double transitionAt{0.0};
	float cost{0.0F};
	bool paid{false};
	bool transitioned{false};
	/** A buffered next dodge: its start. */
	std::optional<double> next;
};

/** The stamina of the character and its dodge, on the clock of the input steps (seconds). */
class Stamina {
public:
	void set_data(const StaminaData& data);
	[[nodiscard]] const StaminaData& data() const { return m_data; }
	void set_load(LoadClass load) { m_load = load; }
	/** The multiplier of the regeneration (1 + StaminaRate of the statuses; field 5 of the Stamina state). */
	void set_rate_multiplier(float multiplier) { m_rateMultiplier = multiplier; }
	/** @return The regeneration in one second (m_regenRate x the load factor x the multiplier). */
	[[nodiscard]] float rate() const { return m_data.regenRate * m_data.regenMult[static_cast<std::size_t>(m_load)] * m_rateMultiplier; }
	[[nodiscard]] LoadClass load() const { return m_load; }
	/** A world entry, a revival, a respawn: the full stamina, no dodge. */
	void refill(double now);

	/** The time passes to `now`: the marks of the dodge that came (the payment, CanTransition, a buffered next dodge), then the
	 *  regeneration. */
	void advance(double now);
	/** @return True when a dodge can start now (Stamina::can_dodge: the data, the load class, not exhausted, the stamina above 0). */
	[[nodiscard]] bool can_dodge() const;
	/** A dodge request at `now` (Stamina::dodge). */
	DodgeVerdict dodge(double now);
	/** A payment (a fragment of an attack): the value stops at 0. */
	void pay(double now, float cost);
	/** The value of the server copy in place of the prediction (Driver::set_server_stamina). */
	void correct(double now, float value);

	[[nodiscard]] float value() const { return m_value; }
	[[nodiscard]] float maximum() const { return m_data.max; }
	[[nodiscard]] bool exhausted() const { return m_exhausted; }
	/** @return The winded time that is left at `now` (s). */
	[[nodiscard]] float winded(double now) const { return m_windedUntil > now ? static_cast<float>(m_windedUntil - now) : 0.0F; }
	/** The dodge that plays (or the last one), and the count of the verdicts. */
	[[nodiscard]] const std::optional<DodgeState>& dodge_state() const { return m_dodge; }
	[[nodiscard]] const std::array<std::uint32_t, kDodgeVerdicts>& verdicts() const { return m_verdicts; }
	[[nodiscard]] std::uint32_t payments() const { return m_payments; }

private:
	DodgeVerdict start_checks() const;
	void start(double at);
	void change(double now, float amount);
	void regenerate(double now);

	StaminaData m_data;
	LoadClass m_load{LoadClass::light};
	float m_rateMultiplier{1.0F};
	float m_value{100.0F};
	double m_regenFrom{0.0};
	double m_heldUntil{0.0};
	double m_windedUntil{0.0};
	double m_last{0.0};
	bool m_exhausted{false};
	std::optional<DodgeState> m_dodge;
	std::uint32_t m_count{0};
	std::array<std::uint32_t, kDodgeVerdicts> m_verdicts{};
	std::uint32_t m_payments{0};
};

} // namespace swc::combat
