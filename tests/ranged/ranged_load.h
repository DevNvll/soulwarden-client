// The reader of the ranged programs of actions.json for the plain tests (stage P3.6): the same fields as the engine reader
// (Character/SwcActions.cpp ReadRanged), with nlohmann::json.

#pragma once

#include <fstream>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "Combat/SwcRanged.h"

namespace swc::ranged::test {

using Json = nlohmann::json;

inline std::string lower_text(std::string text)
{
	for (char& c : text) {
		if (c >= 'A' && c <= 'Z') {
			c = static_cast<char>(c - 'A' + 'a');
		}
	}
	return text;
}

inline Node node_of(const Json& value)
{
	Node node;
	node.type = value[0].get<std::string>();
	for (const auto& [key, attr] : value[1].items()) {
		node.attrs[key] = attr.is_string() ? attr.get<std::string>() : attr.dump();
	}
	for (const Json& child : value[2]) {
		node.children.push_back(node_of(child));
	}
	return node;
}

inline ItemKind kind_of(const std::string& type)
{
	if (type == "CAGE-TransitionInstant") {
		return ItemKind::transitionInstant;
	}
	if (type == "CAGE-TransitionBuffered") {
		return ItemKind::transitionBuffered;
	}
	if (type == "CAGE-RangedAttack") {
		return ItemKind::rangedAttack;
	}
	if (type == "CAGE-CastSpell") {
		return ItemKind::castSpell;
	}
	if (type == "CAGE-Named") {
		return ItemKind::named;
	}
	if (type == "CAGE-PayManaCost") {
		return ItemKind::payMana;
	}
	if (type == "CAGE-Damage") {
		return ItemKind::damage;
	}
	return ItemKind::other;
}

inline Program program_of(const Json& weapon)
{
	Program program;
	for (const Json& tag : weapon["tags"]) {
		program.weaponTags.insert(lower_text(tag.get<std::string>()));
	}
	const Json& ranged = weapon["ranged"];
	for (const auto& [name, value] : ranged["fragments"].items()) {
		Fragment fragment;
		fragment.name = name;
		for (const Json& entry : value) {
			Variant variant;
			for (const Json& tag : entry["tags"]) {
				variant.tags.insert(lower_text(tag.get<std::string>()));
			}
			variant.record = entry["fragment"].get<std::string>();
			variant.length = entry.value("length", 0.0F);
			variant.loop = entry.value("loop", false);
			for (const Json& row : entry["items"]) {
				Item item;
				item.kind = kind_of(row[0].get<std::string>());
				item.start = row[1].get<float>();
				item.end = row[2].is_null() ? -1.0F : row[2].get<float>();
				for (const auto& [key, param] : row[3].items()) {
					item.params[key] = param.get<std::string>();
				}
				variant.items.push_back(item);
			}
			fragment.variants.push_back(variant);
		}
		program.fragments[lower_text(name)] = fragment;
	}
	for (const auto& [name, value] : ranged["actions"].items()) {
		Action action;
		action.name = name;
		action.fragment = value["fragment"].get<std::string>();
		if (!value["activation"].is_null()) {
			action.activation = node_of(value["activation"]);
		}
		if (!value["deactivation"].is_null()) {
			action.deactivation = node_of(value["deactivation"]);
		}
		for (const Json& row : value["activities"]) {
			Activity activity;
			activity.type = row[0].get<std::string>();
			for (const auto& [key, attr] : row[1].items()) {
				activity.attrs[key] = attr.get<std::string>();
			}
			if (!row[2].is_null()) {
				activity.guard = node_of(row[2]);
			}
			action.activities.push_back(activity);
		}
		program.actions[lower_text(name)] = action;
	}
	for (const Json& value : ranged["abilities"]) {
		Ability ability;
		ability.id = value["id"].get<std::string>();
		ability.crc = value["crc"].get<std::uint32_t>();
		for (const Json& start : value["actions"]) {
			ability.starts.push_back({start["action"].get<std::string>(), start["self"].get<bool>()});
		}
		ability.cooldown = value["cooldown"].is_null() ? -1.0F : value["cooldown"].get<float>();
		ability.minCooldown = value["minCooldown"].is_null() ? -1.0F : value["minCooldown"].get<float>();
		ability.costId = value["costId"].get<std::string>();
		ability.manaCost = value["manaCost"].get<float>();
		program.abilities.push_back(ability);
	}
	for (const auto& [name, value] : ranged["conditions"].items()) {
		program.conditions[lower_text(name)] = node_of(value);
	}
	for (const Json& name : ranged["start"]) {
		program.start.push_back(name.get<std::string>());
	}
	for (const Json& name : ranged["order"]) {
		program.order.push_back(name.get<std::string>());
	}
	program.ammo = ranged.value("ammo", std::string());
	return program;
}

inline Shared shared_of(const Json& doc)
{
	Shared shared;
	const Json& ranged = doc["ranged"];
	for (const Json& row : ranged["shots"]) {
		ShotRule rule;
		rule.profile = row["profile"].get<std::string>();
		rule.attack = row["attack"].get<std::string>();
		rule.attackCrc = row["attackCrc"].get<std::uint32_t>();
		rule.row = row["row"].get<std::string>();
		rule.rowCrc = row["rowCrc"].get<std::uint32_t>();
		rule.charge = row["charge"].get<float>();
		rule.spell = row["spell"].get<std::string>();
		rule.heavy = row["heavy"].get<bool>();
		rule.ability = row["ability"].get<bool>();
		if (!row["flight"].is_null()) {
			const Json& flight = row["flight"];
			rule.flight.known = true;
			rule.flight.speed = flight["speed"].get<float>();
			for (std::size_t axis = 0; axis < 3; ++axis) {
				rule.flight.accel[axis] = flight["accel"][axis].get<float>();
			}
			rule.flight.life = flight["life"].get<float>();
			rule.flight.accelDelay = flight["accelDelay"].get<float>();
			rule.flight.maxRange = flight["maxRange"].get<float>();
			rule.flight.radius = flight["radius"].get<float>();
			rule.flight.nearRange = flight["nearRange"].get<float>();
		}
		shared.shots.push_back(rule);
	}
	for (const auto& [name, value] : ranged["spells"].items()) {
		Spell spell;
		spell.id = value["id"].get<std::string>();
		spell.types = value["types"].get<std::string>();
		spell.attack = value["attack"].get<std::string>();
		spell.attackCrc = value["attackCrc"].get<std::uint32_t>();
		spell.castDistance = value["castDistance"].is_null() ? 0.0F : value["castDistance"].get<float>();
		spell.castHeight = value["castHeight"].is_null() ? 0.0F : value["castHeight"].get<float>();
		spell.radius = value["radius"].is_null() ? 0.0F : value["radius"].get<float>();
		spell.height = value["height"].is_null() ? 0.0F : value["height"].get<float>();
		spell.duration = value["duration"].is_null() ? 0.0F : value["duration"].get<float>();
		spell.cameraLock = value["cameraLock"].get<bool>();
		spell.manaCost = value.value("manaCost", std::string());
		shared.spells[name] = spell;
	}
	for (const auto& [name, value] : ranged["manaCosts"].items()) {
		shared.manaCosts[name] = value.get<float>();
	}
	for (const auto& [name, value] : ranged["weapons"].items()) {
		shared.weapons[name] = {value["profile"].get<std::string>(), value["ammo"].get<std::string>()};
	}
	if (ranged.contains("tagGroups")) {
		for (const auto& [name, value] : ranged["tagGroups"].items()) {
			for (const auto& tag : value) {
				shared.tagGroups[name].insert(tag.get<std::string>());
			}
		}
	}
	return shared;
}

/** The actions.json of a path, and the program of each ranged tag set ("Bow,LongBow" -> program). */
struct Data {
	Shared shared;
	std::map<std::string, Program> programs;
};

inline bool load(const std::string& path, Data& out)
{
	std::ifstream input(path);
	if (!input) {
		return false;
	}
	const Json doc = Json::parse(input);
	out.shared = shared_of(doc);
	for (const Json& weapon : doc["weapons"]) {
		if (!weapon.contains("ranged")) {
			continue;
		}
		std::string key;
		for (const Json& tag : weapon["tags"]) {
			key += (key.empty() ? "" : ",") + tag.get<std::string>();
		}
		out.programs[key] = program_of(weapon);
	}
	return true;
}

} // namespace swc::ranged::test
