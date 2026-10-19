#include "Character/SwcMoveRules.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace swc::move {
namespace {

/** The depth limit of a condition tree (a cycle of the data cannot loop). */
constexpr int kMaxDepth = 24;

std::string attr(const Condition& condition, const char* name)
{
	const auto found = condition.attrs.find(name);
	return found != condition.attrs.end() ? found->second : std::string();
}

bool truth(const std::string& text, bool fallback)
{
	if (text.empty()) {
		return fallback;
	}
	return text == "true" || text == "True" || text == "TRUE" || text == "1";
}

bool compare(float value, const std::string& comparison, float reference)
{
	if (comparison == "GreaterThanOrEqual") {
		return value >= reference;
	}
	if (comparison == "GreaterThan") {
		return value > reference;
	}
	if (comparison == "LessThan") {
		return value < reference;
	}
	if (comparison == "LessThanOrEqual") {
		return value <= reference;
	}
	if (comparison == "Equal" || comparison == "EqualTo") {
		return value == reference;
	}
	return false;
}

bool evaluate(const Data& data, const Condition& condition, const Facts& facts, int depth);

bool named(const Data& data, std::string_view name, const Facts& facts, int depth)
{
	if (depth > kMaxDepth) {
		return false;
	}
	const auto found = data.conditions.find(std::string(name));
	if (found != data.conditions.end()) {
		return evaluate(data, found->second, facts, depth + 1);
	}
	// A name with no condition is a Mannequin tag state: each tag of the "+" list is set.
	std::size_t begin = 0;
	while (begin <= name.size()) {
		const std::size_t end = std::min(name.find('+', begin), name.size());
		const std::string tag(name.substr(begin, end - begin));
		if (!tag.empty() && facts.tags.count(tag) == 0) {
			return false;
		}
		begin = end + 1;
	}
	return !name.empty();
}

bool evaluate(const Data& data, const Condition& condition, const Facts& facts, int depth)
{
	if (depth > kMaxDepth) {
		return false;
	}
	const std::string& type = condition.type;
	if (type == "And") {
		return std::all_of(condition.children.begin(), condition.children.end(), [&](const Condition& child) { return evaluate(data, child, facts, depth + 1); });
	}
	if (type == "Or") {
		return std::any_of(condition.children.begin(), condition.children.end(), [&](const Condition& child) { return evaluate(data, child, facts, depth + 1); });
	}
	if (type == "Not") {
		return condition.children.empty() || !evaluate(data, condition.children.front(), facts, depth + 1);
	}
	if (type == "True") {
		return true;
	}
	if (type == "False") {
		return false;
	}
	if (type == "IfMannequinTag") {
		return facts.tags.count(attr(condition, "TagName")) > 0;
	}
	if (type == "IfInput") {
		return facts.inputs.count(attr(condition, "InputName")) > 0;
	}
	if (type == "IfMannequinMarker") {
		return facts.markers.count(attr(condition, "MarkerName")) > 0;
	}
	if (type == "IfEncumbered") {
		// The attribute immobilized (a larger factor) is not used in this client: no live value (research p33-movement.md Q4).
		return !truth(attr(condition, "immobilized"), false) && facts.encumbered;
	}
	if (type == "IfCanRun") {
		return truth(attr(condition, "UseServer"), false) ? facts.serverCanRun : facts.canRun;
	}
	if (type == "IfCanSprint") {
		return truth(attr(condition, "UseServer"), false) ? facts.serverCanSprint : facts.canSprint;
	}
	if (type == "IfCanNav") {
		return facts.canNav;
	}
	if (type == "IfCanDodge") {
		return facts.canDodge;
	}
	if (type == "ActionConditionEquipLoadCategory") {
		return attr(condition, "Value") == equip_load_text(facts.equipLoad);
	}
	if (type == "IfPlayerSetting") {
		return facts.settings.count(attr(condition, "PlayerSettingName")) > 0;
	}
	if (type == "IfCameraLockActive") {
		return facts.cameraLock;
	}
	if (type == "IfExternalCondition") {
		return attr(condition, "ConditionName") == "Exhausted" && facts.exhausted;
	}
	if (type == "MoveSpeed") {
		return compare(facts.moveSpeed, attr(condition, "ComparisonType"), std::strtof(attr(condition, "Value").c_str(), nullptr));
	}
	if (type == "IfWaterDepth") {
		// The depth class of the feet against the value of the condition (stage WF5; the comparison codes of the retail reader, client
		// code 0x2C5B350: Equal, LessThan, LessThanOrEqual, GreaterThan, GreaterThanOrEqual; no comparison: Equal).
		const std::string value = attr(condition, "WaterDepth");
		const int wanted = value == "VeryShallow" ? 1 : value == "Shallow" ? 2 : value == "Deep" ? 3 : 0;
		const int water = facts.waterDepth;
		const std::string comparison = attr(condition, "Comparison");
		return comparison == "LessThan" ? water < wanted : comparison == "LessThanOrEqual" ? water <= wanted : comparison == "GreaterThan" ? water > wanted
			: comparison == "GreaterThanOrEqual" ? water >= wanted : water == wanted;
	}
	if (type == "IfNamedCondition" || type == "IfCondition") {
		return named(data, attr(condition, "ConditionName"), facts, depth + 1);
	}
	// IfFragmentPlaying, IfAliasStatus, IfStackConfigVar, IfStatusEffect, ...: not a fact of this client.
	return false;
}

} // namespace

bool StatusRow::has_category(std::string_view category) const
{
	std::size_t begin = 0;
	while (begin <= categories.size()) {
		const std::size_t end = std::min(categories.find('+', begin), categories.size());
		if (std::string_view(categories).substr(begin, end - begin) == category) {
			return true;
		}
		begin = end + 1;
	}
	return false;
}

const char* equip_load_text(EquipLoad load)
{
	switch (load) {
	case EquipLoad::fast:
		return "Fast";
	case EquipLoad::normal:
		return "Normal";
	case EquipLoad::slow:
		return "Slow";
	case EquipLoad::overburdened:
		return "Overburdened";
	}
	return "Fast";
}

bool holds(const Data& data, std::string_view name, const Facts& facts)
{
	return named(data, name, facts, 0);
}

bool holds(const Data& data, const Condition& condition, const Facts& facts)
{
	return evaluate(data, condition, facts, 0);
}

Planar camera_relative_target(Planar direction, Planar speed, float backwards)
{
	const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
	if (length <= 0.0F) {
		return {};
	}
	const Planar unit{direction.x / length, direction.y / length};
	const float side = unit.x * speed.x;
	const float ahead = unit.y * speed.y;
	const float size = std::sqrt(side * side + ahead * ahead);
	Planar target{unit.x * size, unit.y * size};
	if (target.y < 0.0F) {
		const float factor = 1.0F - std::abs(unit.y) * (1.0F - backwards);
		target.x *= factor;
		target.y *= factor;
	}
	return target;
}

Planar camera_relative_step(Planar current, Planar target, Planar accel, Planar decel, float seconds)
{
	const Planar difference{target.x - current.x, target.y - current.y};
	const float distance = std::sqrt(difference.x * difference.x + difference.y * difference.y);
	if (distance <= 0.0F) {
		return target;
	}
	const Planar rate{std::abs(current.x) > std::abs(target.x) ? decel.x : accel.x, std::abs(current.y) > std::abs(target.y) ? decel.y : accel.y};
	const Planar unit{difference.x / distance, difference.y / distance};
	const float stepX = rate.x * unit.x;
	const float stepY = rate.y * unit.y;
	const float step = std::sqrt(stepX * stepX + stepY * stepY) * seconds;
	if (step >= distance) {
		return target;
	}
	return {current.x + unit.x * step, current.y + unit.y * step};
}

const Motion* motion_of(const Data& data, std::string_view action, const Facts& facts)
{
	const auto found = data.motions.find(std::string(action));
	if (found == data.motions.end()) {
		return nullptr;
	}
	for (const Motion& motion : found->second.conditionals) {
		if (holds(data, motion.name, facts)) {
			return &motion;
		}
	}
	return &found->second.fallback;
}

float facing_of(const Data& data, std::string_view action, const Facts& facts)
{
	const auto found = data.facings.find(std::string(action));
	if (found == data.facings.end()) {
		return 0.0F;
	}
	for (const auto& [name, rate] : found->second.conditionals) {
		if (holds(data, name, facts)) {
			return rate;
		}
	}
	return found->second.fallback;
}

const char* posture_text(Posture posture)
{
	switch (posture) {
	case Posture::stand:
		return "stand";
	case Posture::crouch:
		return "crouch";
	case Posture::prone:
		return "prone";
	}
	return "stand";
}

const char* posture_tag(Posture posture)
{
	switch (posture) {
	case Posture::stand:
		return "Stand";
	case Posture::crouch:
		return "Crouch";
	case Posture::prone:
		return "Prone";
	}
	return "Stand";
}

void PostureState::toggle_crouch()
{
	m_desired = m_desired == Posture::crouch ? Posture::stand : Posture::crouch;
}

void PostureState::toggle_prone()
{
	m_desired = m_desired == Posture::prone ? Posture::stand : Posture::prone;
}

void PostureState::force(Posture posture)
{
	m_desired = posture;
	m_posture = posture;
}

std::optional<Posture> PostureState::pending() const
{
	return m_desired != m_posture ? std::optional<Posture>(m_desired) : std::nullopt;
}

Transition transition_of(Posture from, Posture to, bool sprinting, bool moving)
{
	// The fragments and the marks CanTransition of the data (player.adb Idle_PostureTransition, Sprint_PostureTransition) and the
	// forced motion of the sprint transitions (playeractions_sprintposetransition; research p33-movement.md Q1).
	Transition out;
	if (sprinting) {
		if (from == Posture::stand && to == Posture::crouch) {
			// The slide: forced 2.75 m/s, decel 10 m/s2, then 20 after CanTransition (0.5 s).
			return {"Sprint_PostureTransition+Crouch+FromStand", 0.5F, true, 2.75F, 10.0F, 20.0F};
		}
		if (from == Posture::stand && to == Posture::prone) {
			// The dive: forced 0.1 m/s, decel 8, then 30 after CanTransition (0.733 s).
			return {"Sprint_PostureTransition+Prone+FromStand", 0.733F, true, 0.1F, 8.0F, 30.0F};
		}
		if (from == Posture::prone && to == Posture::stand) {
			return {"Sprint_PostureTransition+Stand+FromProne", 0.833F, true, 0.1F, 10.0F, 10.0F};
		}
		if (from == Posture::crouch && to == Posture::prone) {
			return {"Sprint_PostureTransition+Prone+FromCrouch", 0.733F, true, 0.1F, 8.0F, 30.0F};
		}
		if (from == Posture::prone && to == Posture::crouch) {
			return {"Sprint_PostureTransition+Crouch+FromProne", 0.8125F, true, 0.1F, 8.0F, 8.0F};
		}
		// A crouch to stand in a sprint: the tag at once (facing 1000 deg/s).
		return out;
	}
	if (moving) {
		// While moving in Nav (StrafePoseTransition_*): the tag at once, with no fragment [D idleposetransition; for a change other
		// than crouch to stand an inference].
		return out;
	}
	const auto idle = [](const char* fragment, float mark) { return Transition{fragment, mark}; };
	if (from == Posture::stand && to == Posture::crouch) {
		return idle("Idle_PostureTransition+Crouch+FromStand", 0.0F);
	}
	if (from == Posture::crouch && to == Posture::stand) {
		return idle("Idle_PostureTransition+Stand+FromCrouch", 0.0F);
	}
	if (from == Posture::stand && to == Posture::prone) {
		return idle("Idle_PostureTransition+Prone+FromStand", 0.4F);
	}
	if (from == Posture::prone && to == Posture::stand) {
		return idle("Idle_PostureTransition+Stand+FromProne", 0.5F);
	}
	if (from == Posture::prone && to == Posture::crouch) {
		return idle("Idle_PostureTransition+Crouch+FromProne", 0.8125F);
	}
	if (from == Posture::crouch && to == Posture::prone) {
		return idle("Idle_PostureTransition+Prone+FromCrouch", 0.667F);
	}
	return out;
}

float capsule_half_height(Posture posture)
{
	// The radius 41 cm plus the half segment: stand 40 cm (research R2.6: the segment 0.8 m), crouch 17 cm, crawl 1 cm.
	switch (posture) {
	case Posture::stand:
		return 81.0F;
	case Posture::crouch:
		return 58.0F;
	case Posture::prone:
		return 42.0F;
	}
	return 81.0F;
}

bool Limits::status(bool StatusRow::*column) const
{
	return std::any_of(statuses.begin(), statuses.end(), [column](const StatusRow* row) { return row != nullptr && row->*column; });
}

bool Limits::slowed() const
{
	return std::any_of(statuses.begin(), statuses.end(), [](const StatusRow* row) { return row != nullptr && row->has_category("Slow"); });
}

bool Limits::can_sprint() const
{
	// IfCanSprint: the server bit and NoSprint; the sprint actions also need not IfEncumbered and not Overburdened [D sprint].
	return serverSprint && !status(&StatusRow::noSprint) && !encumbered_movement() && equipLoad != EquipLoad::overburdened;
}

bool Limits::can_dodge() const
{
	// IfCanDodge (NoDodge, the server bit), not encumbered (the backend refuses a dodge above the limit), not Overburdened [D dodge].
	return serverDodge && !status(&StatusRow::noDodge) && !stunned() && !encumbered() && equipLoad != EquipLoad::overburdened;
}

bool Limits::can_jump() const
{
	// Jump: IfCanNav, IfCanDodge, not IfEncumbered, not Overburdened [D traversal].
	return can_nav() && serverDodge && !status(&StatusRow::noDodge) && !encumbered_movement() && equipLoad != EquipLoad::overburdened;
}

float Limits::speed_multiplier() const
{
	return std::clamp(sprintMultiplier, 0.0F, 1.5F);
}

float Limits::dodge_multiplier() const
{
	return std::min(speed_multiplier(), 1.0F);
}

void Limits::apply(Facts& facts) const
{
	facts.encumbered = encumbered_movement();
	facts.canRun = can_run();
	facts.canSprint = can_sprint();
	facts.canNav = can_nav();
	facts.canDodge = serverDodge && !status(&StatusRow::noDodge);
	facts.serverCanRun = serverRun;
	facts.serverCanSprint = serverSprint;
	facts.equipLoad = equipLoad;
	facts.exhausted = exhausted;
}

bool AutoRun::toggle()
{
	m_on = !m_on;
	return m_on;
}

std::pair<float, float> AutoRun::input(float side, float forward) const
{
	return m_on ? std::pair<float, float>(side, 1.0F) : std::pair<float, float>(side, forward);
}

} // namespace swc::move
