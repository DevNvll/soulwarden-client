#include "Combat/SwcRanged.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace swc::ranged {
namespace {

/** The depth limit of a condition tree (a named condition that names itself). */
constexpr int kDepthLimit = 24;
/** The ammunition setting of the supported build: the bow actions open with no arrow (protocol notes 4.27, live L1). */
constexpr std::string_view kAmmolessSetting = "javelin.enable-ammoless-weapons";

std::string lower(std::string_view text)
{
	std::string out(text);
	for (char& c : out) {
		if (c >= 'A' && c <= 'Z') {
			c = static_cast<char>(c - 'A' + 'a');
		}
	}
	return out;
}

bool same(std::string_view a, std::string_view b)
{
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t index = 0; index < a.size(); ++index) {
		char x = a[index];
		char y = b[index];
		x = (x >= 'A' && x <= 'Z') ? static_cast<char>(x - 'A' + 'a') : x;
		y = (y >= 'A' && y <= 'Z') ? static_cast<char>(y - 'A' + 'a') : y;
		if (x != y) {
			return false;
		}
	}
	return true;
}

float number(std::string_view text, float fallback)
{
	if (text.empty()) {
		return fallback;
	}
	const std::string copy(text);
	char* end = nullptr;
	const float value = std::strtof(copy.c_str(), &end);
	return end != copy.c_str() ? value : fallback;
}

/** @return The CRC-32 of the lower-case text (gamedata::progression_id of the backend: an ability id, a ranged attack, a row). */
std::uint32_t crc_of(std::string_view text)
{
	std::uint32_t crc = 0xFFFFFFFFU;
	for (char c : text) {
		const auto byte = static_cast<std::uint8_t>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
		crc ^= byte;
		for (int bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
		}
	}
	return ~crc;
}

/** @return True for an input name of the data in one step (the bindings of the default profile; research p36-data.md Q2.1). */
bool input_at(const Input& in, std::string_view name, bool aimBlocked)
{
	if (name == "attack_shoot") {
		return in.attackPress;
	}
	if (name == "attack_primary_virtual" || name == "attack_primary" || name == "aim_shoot_depressed") {
		return in.attackDown;
	}
	if (name == "attack_primary_hold" || name == "aim_shoot_hold") {
		return in.attackDown && !in.attackPress;
	}
	if (name == "attack_primary_release" || name == "aim_shoot_release" || name == "aim_shoot_release_virtual") {
		return in.attackRelease;
	}
	// aim_draw_active_virtual is false while an aim of a bow ability is on (IfToggleHoldInput of playerinput_combat).
	if (name == "aim_draw_active_virtual") {
		return in.aimDown && !aimBlocked;
	}
	if (name == "aim_draw" || name == "block" || name == "block_depressed" || name == "block_active_virtual") {
		return in.aimDown;
	}
	if (name == "aim_draw_press" || name == "aim_draw_press_virtual" || name == "block_press_virtual") {
		return in.aimPress;
	}
	if (name == "aim_draw_release" || name == "aim_draw_release_virtual") {
		return in.aimRelease;
	}
	if (name == "self_target") {
		return in.selfTarget;
	}
	if (name == "dodge") {
		return in.dodge;
	}
	if (name == "Use_MainHandSlot_1" || name == "Use_MainHandSlot_2" || name == "quickslot-weaponSwap") {
		return in.weaponKey;
	}
	// The keys of the UI screens, the building, the camera lock of the group, the action map: not in these runs.
	return false;
}

} // namespace

std::string_view Node::attr(std::string_view key) const
{
	const auto found = attrs.find(std::string(key));
	return found != attrs.end() ? std::string_view(found->second) : std::string_view();
}

std::string_view Item::param(std::string_view key) const
{
	const auto found = params.find(std::string(key));
	return found != params.end() ? std::string_view(found->second) : std::string_view();
}

std::string_view Activity::attr(std::string_view key) const
{
	const auto found = attrs.find(std::string(key));
	return found != attrs.end() ? std::string_view(found->second) : std::string_view();
}

const Variant* Fragment::variant_for(const std::set<std::string>& tags) const
{
	const Variant* best = nullptr;
	for (const Variant& variant : variants) {
		const bool fits = std::all_of(variant.tags.begin(), variant.tags.end(), [&](const std::string& tag) { return tags.count(tag) != 0; });
		if (fits && (best == nullptr || variant.tags.size() > best->tags.size())) {
			best = &variant;
		}
	}
	return best;
}

const Spell* Shared::spell(std::string_view id) const
{
	const auto found = spells.find(lower(id));
	return found != spells.end() ? &found->second : nullptr;
}

float Shared::mana_cost(std::string_view costId) const
{
	const auto found = manaCosts.find(lower(costId));
	return found != manaCosts.end() ? found->second : 0.0F;
}

const ShotRule* Shared::rule(std::uint32_t attackCrc, std::uint32_t rowCrc, std::string_view spellId) const
{
	for (const ShotRule& shot : shots) {
		if (!spellId.empty() ? same(shot.spell, spellId) : (shot.spell.empty() && shot.attackCrc == attackCrc && shot.rowCrc == rowCrc)) {
			return &shot;
		}
	}
	return nullptr;
}

const Fragment* Program::fragment(std::string_view name) const
{
	const auto found = fragments.find(lower(name));
	return found != fragments.end() ? &found->second : nullptr;
}

const Action* Program::action(std::string_view name) const
{
	const auto found = actions.find(lower(name));
	return found != actions.end() ? &found->second : nullptr;
}

const Ability* Program::ability(std::uint32_t crc) const
{
	for (const Ability& entry : abilities) {
		if (entry.crc == crc) {
			return &entry;
		}
	}
	return nullptr;
}

const Ability* Program::ability_named(std::string_view id) const
{
	for (const Ability& entry : abilities) {
		if (same(entry.id, id)) {
			return &entry;
		}
	}
	return nullptr;
}

const Node* Program::condition(std::string_view name) const
{
	const auto found = conditions.find(lower(name));
	return found != conditions.end() ? &found->second : nullptr;
}

void Mana::advance(double time)
{
	if (time > m_time) {
		const double from = std::max(m_time, m_regenFrom);
		if (time > from && m_value < m_max) {
			m_value = std::min(m_max, m_value + kBaseRate * m_factor * static_cast<float>(time - from));
		}
		m_time = time;
	}
	// The server copy moved forward to now: its regeneration after the delay that was left.
	if (m_server) {
		const double since = time - m_server->time;
		const float rate = m_server->rate > 0.0F ? m_server->rate : kBaseRate * m_factor;
		const float forward = std::min(m_max, m_server->value + rate * static_cast<float>(std::max(since - m_server->delay, 0.0)));
		if (std::fabs(forward - m_value) > kCorrection && time - m_lastPayment > kCorrectionTime) {
			if (m_differentSince < 0.0) {
				m_differentSince = time;
			} else if (time - m_differentSince >= kCorrectionTime) {
				m_value = forward;
				m_differentSince = -1.0;
				++m_corrections;
			}
		} else {
			m_differentSince = -1.0;
		}
	}
}

bool Mana::pay(double time, float amount)
{
	advance(time);
	if (amount <= 0.0F) {
		return true;
	}
	if (m_value < amount) {
		return false;
	}
	m_value -= amount;
	// The regeneration starts after ManaRegenDelay, or ManaRegenDelayOnEmpty when the payment took the mana to 0.
	m_regenFrom = time + (m_value <= 0.0F ? kDelayOnEmpty : kDelay);
	m_lastPayment = time;
	++m_payments;
	return true;
}

void Mana::set_server(double time, float value, float baseMax, float delay, float rate)
{
	advance(time);
	// The meter of the client: the current value over the base maximum times the MaxMana parts (the client has no parts: the
	// larger of the base maximum and the value).
	m_max = std::max(baseMax > 0.0F ? baseMax : kBaseMax, value);
	if (!m_known) {
		m_known = true;
		m_value = value;
		m_regenFrom = time + delay;
	}
	m_server = Server{time, value, delay, rate};
}

void Mana::force_sync(double time, float value, float baseMax, float delay, float factor)
{
	advance(time);
	m_max = std::max(baseMax > 0.0F ? baseMax : kBaseMax, value);
	m_value = std::min(value, m_max);
	m_factor = factor > 0.0F ? factor : 1.0F;
	m_regenFrom = time + std::max(delay, 0.0F);
	m_known = true;
	m_server = Server{time, value, delay, 0.0F};
	m_differentSince = -1.0;
}

void Player::set_program(const Program* program, const Shared* shared)
{
	m_program = program;
	m_shared = shared;
	reset();
}

void Player::reset()
{
	m_active.clear();
	m_playing = Playing{};
	m_tags.clear();
	m_fired.clear();
	m_buffered.clear();
	m_abilityInputs.clear();
	m_ability = 0;
	m_raycast = 0;
	m_targeting.clear();
	m_channel.clear();
	m_facingRate = -1.0F;
	m_forceWalk = false;
}

bool Player::marker(std::string_view name) const
{
	if (m_playing.variant == nullptr) {
		return false;
	}
	for (const Item& item : m_playing.variant->items) {
		if (item.kind == ItemKind::named && same(item.param("Name"), name) && m_playing.clock >= item.start - kClockEpsilon
			&& (item.end < 0.0F || m_playing.clock < item.end - kClockEpsilon)) {
			return true;
		}
	}
	return false;
}

bool Player::aim_down_sights() const
{
	return m_tags.count("aimdownsights") != 0;
}

bool Player::force_walk() const
{
	return m_forceWalk || marker("ForceWalk");
}

bool Player::has_input(std::string_view name, unsigned backTicks) const
{
	const bool aimBlocked = m_program != nullptr && m_program->weaponTags.count("bow") != 0
		&& (m_tags.count("heavyaim") != 0 || m_tags.count("penetratingaim") != 0 || m_tags.count("smokeaim") != 0);
	for (unsigned age = 0; age <= backTicks && age < m_history.size(); ++age) {
		if (input_at(m_history[age], name, aimBlocked)) {
			return true;
		}
	}
	return false;
}

bool Player::named(std::string_view name, const Facts& facts, const Action* host) const
{
	const Node* node = m_program != nullptr ? m_program->condition(name) : nullptr;
	if (node == nullptr) {
		++m_unknown["named:" + std::string(name)];
		return false;
	}
	return holds(*node, facts, host);
}

bool Player::holds(const Node& node, const Facts& facts, const Action* host) const
{
	thread_local int depth = 0;
	if (depth > kDepthLimit) {
		return false;
	}
	++depth;
	bool value = false;
	if (node.type == "And") {
		value = std::all_of(node.children.begin(), node.children.end(), [&](const Node& child) { return holds(child, facts, host); });
	} else if (node.type == "Or") {
		value = std::any_of(node.children.begin(), node.children.end(), [&](const Node& child) { return holds(child, facts, host); });
	} else if (node.type == "Not") {
		value = node.children.empty()
			|| !std::all_of(node.children.begin(), node.children.end(), [&](const Node& child) { return holds(child, facts, host); });
	} else {
		value = leaf(node, facts, host);
	}
	--depth;
	return value;
}

bool Player::leaf(const Node& node, const Facts& facts, const Action* host) const
{
	const std::string& type = node.type;
	if (type == "True") {
		return true;
	}
	if (type == "False") {
		return false;
	}
	if (type == "IfInput") {
		return has_input(node.attr("InputName"), static_cast<unsigned>(number(node.attr("BackTicks"), 0.0F)));
	}
	if (type == "IfMannequinTag") {
		const std::string tag = lower(node.attr("TagName"));
		if (m_program != nullptr && m_program->weaponTags.count(tag) != 0) {
			return true;
		}
		// The posture of these weapons is Stand (crouch and prone are in the movement of stage P3.3); Idle while the character
		// does not move.
		if (tag == "stand" || tag == "stand_desired") {
			return true;
		}
		if (tag == "idle") {
			return !facts.moving;
		}
		return m_tags.count(tag) != 0;
	}
	if (type == "IfMannequinMarker") {
		return marker(node.attr("MarkerName"));
	}
	if (type == "IfExternalCondition") {
		const std::string_view name = node.attr("ConditionName");
		if (m_program != nullptr && m_program->condition(name) != nullptr) {
			return named(name, facts, host);
		}
		// The conditions of the engine: the startup of an ability is its mark, the staffs attack with the upper body.
		if (name == "Ability_Startup") {
			return marker("Ability_Startup");
		}
		if (name == "UseUpperBodyAttacks") {
			return true;
		}
		// Trigger_From_Sheathed (the weapon is drawn when the program plays), Throwable_Equipped, IsMelee, Prevent_Attack,
		// Prevent_BasicAttack, Allow_Stamina_Regen_Bonus, Has_Valid_Current_Offhand, the artifacts: no.
		return false;
	}
	if (type == "IfActionStatus") {
		if (node.attr("Status") != "Installed") {
			return false;
		}
		return std::any_of(m_active.begin(), m_active.end(), [&](const Active& active) { return same(active.action->name, node.attr("ActionName")); });
	}
	if (type == "IfItemInSlot") {
		return node.attr("SlotToCheckAlias") != "THROWABLE_OBJECT_SLOT";
	}
	if (type == "IfMannequinTagInItemSlot") {
		return node.attr("SlotToCheckAlias") == "ACTIVE_MAIN_HAND_WEAPON" && m_program != nullptr
			&& m_program->weaponTags.count(lower(node.attr("TagName"))) != 0;
	}
	if (type == "IfActiveAbilityMoveName") {
		const auto found = m_abilityInputs.find(crc_of(node.attr("AbilityName")));
		return found != m_abilityInputs.end()
			&& static_cast<std::uint16_t>(m_input - found->second) <= static_cast<unsigned>(number(node.attr("BackTicks"), 0.0F));
	}
	if (type == "IfHasStatusEffect") {
		return facts.statuses.count(lower(node.attr("StatusEffect"))) != 0;
	}
	if (type == "IfFragmentPlaying") {
		if (!node.attr("FragmentName").empty()) {
			return m_playing.fragment != nullptr && same(m_playing.fragment->name, node.attr("FragmentName"));
		}
		// UseHostActionFragment: the fragment of the action of the condition plays (an action that is not active: no).
		return m_playing.fragment != nullptr && host != nullptr && m_playing.action == host;
	}
	if (type == "IfFragmentDone") {
		// UseHostActionFragment: the fragment of the action of the condition; an action that lost the scope has its fragment done.
		if (host != nullptr && node.attr("UseHostActionFragment") == "true" && m_playing.action != host) {
			return true;
		}
		const Variant* variant = m_playing.variant;
		return variant != nullptr && !variant->loop && variant->length > 0.0F
			&& m_playing.clock >= variant->length - number(node.attr("ExitOffsetSeconds"), 0.0F) - kClockEpsilon;
	}
	if (type == "HeightToGround") {
		return facts.heightToGround > number(node.attr("ActivationHeight"), 0.0F);
	}
	if (type == "IsSilenced") {
		return facts.silenced;
	}
	if (type == "IsStunned") {
		return facts.stunned;
	}
	if (type == "IfCooldownTimer") {
		return facts.cooldowns.count(crc_of(node.attr("AbilityID"))) != 0;
	}
	if (type == "IfEncumbered") {
		return facts.encumbered;
	}
	if (type == "IfAbilityActive") {
		return facts.nodes.count(crc_of(node.attr("AbilityName"))) != 0;
	}
	if (type == "IfStackConfigVar") {
		return node.attr("StackVar") == kAmmolessSetting;
	}
	if (type == "IfManaCost") {
		const float cost = m_shared != nullptr ? m_shared->mana_cost(node.attr("CostID")) : 0.0F;
		const std::string_view comparison = node.attr("ComparisonType");
		if (comparison == "LessThan") {
			return facts.mana < cost;
		}
		if (comparison == "LessThanOrEqual") {
			return facts.mana <= cost;
		}
		if (comparison == "GreaterThan") {
			return facts.mana > cost;
		}
		if (comparison == "Equal") {
			return facts.mana == cost;
		}
		return facts.mana >= cost;
	}
	if (type == "IfCameraLockActive") {
		return facts.cameraLock;
	}
	if (type == "IfCameraFreeLookActive") {
		return facts.freeLook;
	}
	if (type == "IfPlayerSetting") {
		return facts.settings.count(std::string(node.attr("PlayerSettingName"))) != 0;
	}
	if (type == "ActionConditionSpellSpawnLocationState") {
		return node.attr("Value") == "RaycastPassed" ? m_raycast == 1 : (node.attr("Value") == "RaycastFailed" && m_raycast == 2);
	}
	if (type == "IfDamagedByPowerLevel") {
		return facts.damaged;
	}
	if (type == "IfCanResizeCharacterController" || type == "IfHasWeaponPrerequisites" || type == "IfItemInSlotEquippable"
		|| type == "IfStaminaValue") {
		return true;
	}
	if (type == "IfWaterDepth" || type == "IsMounted" || type == "IfAmmoTypeInSlot" || type == "IfCameraStickyLockActive" || type == "IfInGroup"
		|| type == "IfCameraLockTargetInSpellRange" || type == "IfCameraLockTargetChange" || type == "IfSpellLosFailClient"
		|| type == "IfTeleportPending" || type == "IfItemSheathed") {
		// No water (stage P4.4), no mount, the ammunition setting, no group and no friend lock (its target change), no travel, the
		// weapon is drawn.
		return false;
	}
	++m_unknown[type];
	return false;
}

void Player::cast(std::string_view spellId, std::uint16_t input, std::vector<Event>& out)
{
	Event event;
	event.kind = Event::Kind::cast;
	event.text = std::string(spellId);
	event.input = input;
	event.ability = m_ability;
	out.push_back(event);
	const Spell* spell = m_shared != nullptr ? m_shared->spell(spellId) : nullptr;
	if (spell == nullptr) {
		return;
	}
	// A projectile spell: the shot item of the cast (protocol notes 4.27: the attack is the RangedAttackName of the spell; a magic
	// shot has the row 0, the charge 0, and no ammunition).
	if (spell->projectile()) {
		Event shot;
		shot.kind = Event::Kind::shot;
		shot.text = spell->id;
		shot.attack = spell->attackCrc;
		shot.input = input;
		shot.ability = m_ability;
		out.push_back(shot);
	}
	if (!spell->manaCost.empty() && m_shared->mana_cost(spell->manaCost) > 0.0F) {
		Event mana;
		mana.kind = Event::Kind::mana;
		mana.text = spell->manaCost;
		mana.amount = m_shared->mana_cost(spell->manaCost);
		mana.input = input;
		out.push_back(mana);
	}
}

void Player::start_fragment(std::string_view name, std::uint16_t input)
{
	const Fragment* fragment = m_program != nullptr ? m_program->fragment(name) : nullptr;
	std::set<std::string> tags = m_tags;
	m_playing.fragment = fragment;
	m_playing.variant = fragment != nullptr ? fragment->variant_for(tags) : nullptr;
	m_playing.start = input;
	m_playing.serial = ++m_serial;
	m_playing.clock = 1.0F / kStepsInSecond;
	const std::size_t count = m_playing.variant != nullptr ? m_playing.variant->items.size() : 0;
	m_fired.assign(count, false);
	m_buffered.assign(count, false);
	for (Active& active : m_active) {
		if (!active.action->fragment.empty()) {
			active.doneSince = -1;
		}
	}
}

void Player::start_action(const Action& action, std::uint16_t input, const Facts& facts, std::vector<Event>& out)
{
	if (!action.fragment.empty() && m_playing.action != nullptr) {
		// The new action takes the fragment scope: the action that owned it ends when it has no deactivation condition, else it stays
		// installed to its deactivation.
		for (std::size_t index = m_active.size(); index-- > 0;) {
			if (m_active[index].action != m_playing.action) {
				continue;
			}
			const Action* owner = m_active[index].action;
			m_playing = Playing{};
			// No deactivation condition, or one that holds now that the action lost the scope (IfFragmentDone of its fragment): it ends.
			if (!owner->deactivation || holds(*owner->deactivation, facts, owner)) {
				end_action(index, input, facts, out);
			} else {
				m_active[index].lostSince = input;
				m_active[index].doneSince = -1;
			}
		}
		m_playing = Playing{};
	}
	Active active;
	active.action = &action;
	active.on.assign(action.activities.size(), false);
	active.delay.assign(action.activities.size(), -1);
	// The tags of the enter first: the variant of the fragment of the action takes them (PenetratingAim of Bow_Penetrating_Ready).
	for (std::size_t index = 0; index < action.activities.size(); ++index) {
		const Activity& activity = action.activities[index];
		if (activity.type == "SetMannequinTag" && !activity.guard && number(activity.attr("DelayTimer"), 0.0F) <= 0.0F) {
			apply(activity, true, input, facts, out);
			active.on[index] = true;
		}
	}
	m_active.push_back(active);
	if (!action.fragment.empty()) {
		m_playing.action = &action;
		start_fragment(action.fragment, input);
	}
	run_activities(m_active.back(), true, false, input, facts, out);
}

void Player::end_action(std::size_t index, std::uint16_t input, const Facts& facts, std::vector<Event>& out)
{
	Active active = m_active[index];
	m_active.erase(m_active.begin() + static_cast<std::ptrdiff_t>(index));
	run_activities(active, false, true, input, facts, out);
	if (m_playing.action == active.action) {
		m_playing = Playing{};
		m_fired.clear();
		m_buffered.clear();
		m_ability = 0;
		m_raycast = 0;
	}
}

void Player::run_items(std::uint16_t input, const Facts& facts, std::vector<Event>& out)
{
	const Variant* variant = m_playing.variant;
	if (variant == nullptr) {
		return;
	}
	for (std::size_t index = 0; index < variant->items.size(); ++index) {
		const Item& item = variant->items[index];
		if (m_fired[index] || item.kind == ItemKind::named || item.kind == ItemKind::transitionInstant
			|| item.kind == ItemKind::transitionBuffered || item.kind == ItemKind::other || m_playing.clock < item.start - kClockEpsilon) {
			continue;
		}
		m_fired[index] = true;
		if (!item.param("Condition").empty() && !named(item.param("Condition"), facts, m_playing.action)) {
			continue;
		}
		if (item.kind == ItemKind::rangedAttack) {
			// A ranged clip (CAGE-RangedAttack): the shot item with its attack, row, charge, and the ammunition of the weapon.
			Event shot;
			shot.kind = Event::Kind::shot;
			shot.attack = crc_of(item.param("RangedAttackName"));
			shot.row = item.param("DamageTableRow").empty() ? 0 : crc_of(item.param("DamageTableRow"));
			shot.charge = number(item.param("ChargeValue"), 0.0F);
			shot.ammo = m_program->ammo.empty() ? 0 : crc_of(m_program->ammo);
			shot.input = input;
			shot.ability = m_ability;
			out.push_back(shot);
		} else if (item.kind == ItemKind::castSpell) {
			cast(item.param("SpellName"), input, out);
		} else if (item.kind == ItemKind::payMana) {
			Event mana;
			mana.kind = Event::Kind::mana;
			mana.text = std::string(item.param("CostID"));
			mana.amount = m_shared != nullptr ? m_shared->mana_cost(mana.text) : 0.0F;
			mana.input = input;
			out.push_back(mana);
		} else if (item.kind == ItemKind::damage) {
			Event damage;
			damage.kind = Event::Kind::damage;
			damage.text = std::string(item.param("DamageTableRow"));
			damage.input = input;
			out.push_back(damage);
		}
	}
}

bool Player::run_transitions(std::uint16_t input, const Facts& facts, std::vector<Event>& out)
{
	const Variant* variant = m_playing.variant;
	if (variant == nullptr) {
		return false;
	}
	const float clock = m_playing.clock;
	for (std::size_t index = 0; index < variant->items.size(); ++index) {
		const Item& item = variant->items[index];
		if (item.kind != ItemKind::transitionInstant && item.kind != ItemKind::transitionBuffered) {
			continue;
		}
		const bool buffered = item.kind == ItemKind::transitionBuffered;
		const bool inWindow = clock >= item.start - kClockEpsilon && (item.end < 0.0F || clock < item.end - kClockEpsilon);
		bool take = false;
		if (inWindow) {
			bool condition = true;
			if (!item.param("Condition").empty()) {
				condition = named(item.param("Condition"), facts, m_playing.action);
			} else if (!item.param("Input").empty() && item.param("Input") != "Test_AlwaysTrue") {
				condition = has_input(item.param("Input"), static_cast<unsigned>(number(item.param("BackTicks"), 0.0F)));
			}
			if (condition) {
				if (buffered) {
					m_buffered[index] = true;
				} else {
					take = true;
				}
			}
		}
		// A buffered window takes its transition at its end (the rule of the attack graph: Sequence::advance of the backend).
		if (buffered && m_buffered[index] && item.end >= 0.0F && clock >= item.end - kClockEpsilon) {
			take = true;
		}
		if (!take) {
			continue;
		}
		const std::string_view newAction = item.param("NewAction");
		const std::string_view newFragment = item.param("NewFragment");
		if (!newAction.empty()) {
			const Action* action = m_program->action(newAction);
			const std::uint32_t ability = m_ability;
			if (action == nullptr) {
				// An action of a different list (the lower of a different weapon): the fragment action ends.
				for (std::size_t at = m_active.size(); at-- > 0;) {
					if (m_active[at].action == m_playing.action) {
						end_action(at, input, facts, out);
					}
				}
				return true;
			}
			// The start of an ability from a basic fragment (CanAttack_<ability>), or the next action of the same ability.
			const Ability* started = nullptr;
			for (const Ability& entry : m_program->abilities) {
				for (const Ability::Start& start : entry.starts) {
					if (same(start.action, action->name)) {
						started = &entry;
					}
				}
			}
			start_action(*action, input, facts, out);
			m_ability = started != nullptr ? started->crc : ability;
			if (started != nullptr && started->crc != ability) {
				Event event;
				event.kind = Event::Kind::ability;
				event.ability = started->crc;
				event.text = started->id;
				event.input = input;
				out.push_back(event);
			}
			return true;
		}
		if (!newFragment.empty()) {
			start_fragment(newFragment, input);
			return true;
		}
	}
	return false;
}

void Player::apply(const Activity& activity, bool rising, std::uint16_t input, const Facts& facts, std::vector<Event>& out)
{
	const std::string& type = activity.type;
	if (type == "SetMannequinTag") {
		const std::string_view operation = rising ? activity.attr("OnEnter") : activity.attr("OnExit");
		const std::string tag = lower(activity.attr("TagName"));
		if (operation == "Set") {
			m_tags.insert(tag);
		} else if (operation == "Unset") {
			m_tags.erase(tag);
		}
	} else if (type == "ClearActiveAbilityMoveInput") {
		if (rising && activity.attr("OnEnter") == "Clear") {
			m_abilityInputs.erase(crc_of(activity.attr("AbilityName")));
		}
	} else if (type == "CastSpell") {
		if (rising) {
			cast(activity.attr("SpellName"), input, out);
		}
	} else if (type == "PayManaCost") {
		if (rising) {
			Event mana;
			mana.kind = Event::Kind::mana;
			mana.text = std::string(activity.attr("CostID"));
			mana.amount = m_shared != nullptr ? m_shared->mana_cost(mana.text) : 0.0F;
			mana.input = input;
			out.push_back(mana);
		}
	} else if (type == "SetCooldownTimer") {
		// The cooldown starts at the enter of its branch, or at the exit with OnExit Enable (Flamethrower at its exit).
		const std::string_view operation = rising ? activity.attr("OnEnter") : activity.attr("OnExit");
		if ((rising && operation != "NoEffect" && operation != "Disable") || (!rising && operation == "Enable")) {
			Event cooldown;
			cooldown.kind = Event::Kind::cooldown;
			cooldown.text = std::string(activity.attr("AbilityID"));
			cooldown.ability = crc_of(activity.attr("AbilityID"));
			cooldown.input = input;
			out.push_back(cooldown);
		}
	} else if (type == "CastSpellRaycast") {
		if (rising) {
			m_raycast = facts.groundValid ? 1 : 2;
			Event raycast;
			raycast.kind = Event::Kind::raycast;
			raycast.text = std::string(activity.attr("SpellName"));
			raycast.passed = facts.groundValid;
			raycast.input = input;
			out.push_back(raycast);
		}
	}
	// ForceWalk, ForceRun, DisableAutorun, CastSpellTargeting, ChannelSpell, CameraRelativeFacing, ForceCameraLock: states
	// (update_flags); PerformActiveAbility: the ability of the action (the cooldown comes from SetCooldownTimer).
}

void Player::run_activities(Active& active, bool entering, bool exiting, std::uint16_t input, const Facts& facts, std::vector<Event>& out)
{
	const Action& action = *active.action;
	for (std::size_t index = 0; index < action.activities.size(); ++index) {
		const Activity& activity = action.activities[index];
		const bool now = !exiting && (!activity.guard || holds(*activity.guard, facts, &action));
		const bool was = active.on[index];
		if (now && !was) {
			const float delay = activity.type == "SetMannequinTag" ? number(activity.attr("DelayTimer"), 0.0F) : 0.0F;
			if (delay > 0.0F) {
				active.delay[index] = static_cast<int>(std::lround(delay * kStepsInSecond));
			} else {
				apply(activity, true, input, facts, out);
			}
		} else if (!now && was) {
			active.delay[index] = -1;
			apply(activity, false, input, facts, out);
		} else if (now && active.delay[index] > 0 && --active.delay[index] == 0) {
			apply(activity, true, input, facts, out);
		}
		active.on[index] = now;
	}
	(void)entering;
}

void Player::update_flags()
{
	m_targeting.clear();
	m_channel.clear();
	m_facingRate = -1.0F;
	m_forceWalk = false;
	for (const Active& active : m_active) {
		for (std::size_t index = 0; index < active.action->activities.size(); ++index) {
			if (!active.on[index]) {
				continue;
			}
			const Activity& activity = active.action->activities[index];
			if (activity.type == "CastSpellTargeting") {
				m_targeting = std::string(activity.attr("SpellName"));
			} else if (activity.type == "ChannelSpell") {
				m_channel = std::string(activity.attr("SpellName"));
			} else if (activity.type == "CameraRelativeFacing") {
				m_facingRate = number(activity.attr("TurnRate"), 0.0F);
			} else if (activity.type == "ForceWalk") {
				m_forceWalk = true;
			}
		}
	}
}

void Player::step(std::uint16_t input, const Input& in, const Facts& facts, std::vector<Event>& out)
{
	if (m_program == nullptr) {
		return;
	}
	m_input = input;
	m_history.push_front(in);
	while (m_history.size() > kHistorySteps) {
		m_history.pop_back();
	}
	// The ability input of a key of the bar: the ability of its place (IfActiveAbilityMoveName); the largest BackTicks is 20.
	for (std::size_t place = 0; place < 3; ++place) {
		if (in.keyPress[place] && facts.bar[place] != 0) {
			m_abilityInputs[facts.bar[place]] = input;
		}
	}
	for (auto it = m_abilityInputs.begin(); it != m_abilityInputs.end();) {
		it = static_cast<std::uint16_t>(input - it->second) > 20 ? m_abilityInputs.erase(it) : std::next(it);
	}
	if (m_playing.fragment != nullptr) {
		m_playing.clock = static_cast<float>(static_cast<std::uint16_t>(input - m_playing.start) + 1) / kStepsInSecond;
	}
	// The deactivation of the active actions (their conditions in the state of this step).
	for (std::size_t index = m_active.size(); index-- > 0;) {
		const Action& action = *m_active[index].action;
		if (action.deactivation && holds(*action.deactivation, facts, &action)) {
			end_action(index, input, facts, out);
		}
	}
	// The items and the transitions of the fragment at its clock (a transition starts a fragment at this step: its items too).
	for (int count = 0; count < kTransitionsInStep && m_playing.fragment != nullptr; ++count) {
		run_items(input, facts, out);
		if (!run_transitions(input, facts, out)) {
			break;
		}
	}
	// The end of a fragment: an action with no deactivation ends with it; else it holds the last pose to its deactivation, at most
	// kStuckTime.
	if (m_playing.variant != nullptr && !m_playing.variant->loop && m_playing.variant->length > 0.0F
		&& m_playing.clock >= m_playing.variant->length - kClockEpsilon) {
		for (std::size_t index = m_active.size(); index-- > 0;) {
			Active& active = m_active[index];
			if (active.action != m_playing.action) {
				continue;
			}
			if (active.doneSince < 0) {
				active.doneSince = input;
			}
			const float done = static_cast<float>(static_cast<std::uint16_t>(input - active.doneSince)) / kStepsInSecond;
			if (!active.action->deactivation || done >= kStuckTime) {
				if (active.action->deactivation) {
					++m_stuckEnds;
				}
				end_action(index, input, facts, out);
			}
		}
	}
	// The activities of the installed actions at the edges of their conditions (the marks and the inputs of this step), before the
	// activations: the raycast of a confirm (CastSpellRaycast) is then a fact of the activation of the cast action in the same step
	// (else the cooldown state of the server for the ability can come before the next step and block the cast: IfCooldownTimer).
	for (Active& entry : m_active) {
		run_activities(entry, false, false, input, facts, out);
	}
	// An installed action that lost the scope: it ends after kLostTime (a rule of this project).
	for (std::size_t index = m_active.size(); index-- > 0;) {
		const Active& entry = m_active[index];
		if (entry.lostSince >= 0 && entry.action != m_playing.action
			&& static_cast<float>(static_cast<std::uint16_t>(input - entry.lostSince)) / kStepsInSecond >= kLostTime) {
			++m_stuckEnds;
			end_action(index, input, facts, out);
		}
	}
	// The activation of the actions that are not active, in the order of the data (one new fragment action in a step).
	const auto active = [&](const Action& action) {
		return std::any_of(m_active.begin(), m_active.end(), [&](const Active& entry) { return entry.action == &action; });
	};
	bool fragmentStarted = false;
	for (const std::string& name : m_program->order) {
		const Action* action = m_program->action(name);
		if (action == nullptr || active(*action) || !action->activation || (fragmentStarted && !action->fragment.empty())) {
			continue;
		}
		if (!holds(*action->activation, facts, action)) {
			continue;
		}
		const std::uint32_t ability = m_ability;
		const Ability* started = nullptr;
		for (const Ability& entry : m_program->abilities) {
			for (const Ability::Start& start : entry.starts) {
				if (same(start.action, action->name)) {
					started = &entry;
				}
			}
		}
		start_action(*action, input, facts, out);
		if (action->fragment.empty()) {
			continue;
		}
		fragmentStarted = true;
		// An ability start, or the next action of the ability that played (the cast after the raycast of its aim).
		m_ability = started != nullptr ? started->crc : ability;
		if (started != nullptr && started->crc != ability) {
			Event event;
			event.kind = Event::Kind::ability;
			event.ability = started->crc;
			event.text = started->id;
			event.input = input;
			out.push_back(event);
		}
		run_items(input, facts, out);
	}
	update_flags();
}

} // namespace swc::ranged
