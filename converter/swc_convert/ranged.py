"""The ranged and magic actions of the player (stage P3.6, docs/RANGED.md): the bow, the fire staff, and the life staff.

The backend has no attack graph for these weapons (weapon-actions.json has the 12 melee graphs). It takes a basic shot from the
shot item of list 2 and the attack bits of its press (Soulwarden protocol notes 4.27; backend world/projectiles.cpp). Thus the
client plays the action lists and the animation databases of the data, and it sends each shot item at the step of its data
item. The actions converter (actions.py) adds a "ranged" entry to the weapon entry of each tag set of RANGED_SETS:

- actions: each action of the action lists of the tag set (playeractions_archery, playeractions_attack, and the ability list of
  the weapon) that the crawl reaches: its fragment name (empty: an action with no fragment, which runs beside the fragment action),
  its activation and deactivation conditions, and its activities of the client rules
  (ACTIVITY_TYPES: the tags, the casts, the mana payments, the cooldowns, the walk, the ground target, the channel) with the
  condition of their IfThenElse branches (the guard; None: the enter and the exit of the action).
- fragments: each fragment name that a reachable action or transition names, with each variant of the SubADB chain of the tag
  set (its tags, the record key of actions.json "fragments" for the clips, and its timed items: [type, start, end, params] for
  the procedural items CAGE-* of its ProcLayers; start and end in seconds, end None: to the end of the fragment).
- abilities: each ability of the ability table of the weapon (the activation IfActiveAbilityMoveName of an action), its CRC-32
  (the id of the bar), its start actions (with the self-target key: Left Ctrl), its cooldown (ability-data.json), and its mana cost
  (the CostID of IfManaCost of its activation, with the value of ability-data.json manaCosts).
- conditions: the named conditions of the action lists (the condition trees; the client rules of docs/RANGED.md read them by name).

The shared part "ranged" of actions.json: the profiles of the ranged attacks and the slices of the projectiles of the backend copy
(projectile-data.json: the flight of the own projectile picture of the client), and the spells of the casts (the spell tables:
the cast distance, the radius and the height of an area, the RangedAttackName of a projectile spell).
"""
from __future__ import annotations

import json
import xml.etree.ElementTree as ET
import zlib

from . import datasheet, player, source

CAGE = "sharedassets/springboardentitites/playerdata/cage/"
# The tag sets of the stage: their action lists, the actions where the crawl starts (the hip shot and the aim of the bow, the charge
# start of the staffs), the ability table, and the ranged profile of their weapons (the WeaponItemDefinitions rows of the bows, the
# fire staffs, and the life staffs: projectile-data.json "weapons").
RANGED_SETS = {
    "Bow+LongBow": {"lists": ["playeractions_archery", "playeractions_ability_bow"], "start": ["Bow_ReadyWeapon", "Bow_Shoot_HipFire"],
                    "own": ["playeractions_archery", "playeractions_ability_bow"], "table": "BowAbilityTable", "profile": "Bow"},
    "MagicStaff+Fire": {"lists": ["playeractions_attack", "playeractions_ability_firemagic"], "start": ["Attack_Start_Charge_Upperbody"],
                        "own": ["playeractions_ability_firemagic"], "table": "FireMagicAbilityTable", "profile": "FireBall"},
    "MagicStaff+Heal": {"lists": ["playeractions_attack", "playeractions_ability_lifemagic"], "start": ["Attack_Start_Charge_Upperbody"],
                        "own": ["playeractions_ability_lifemagic"], "table": "LifeMagicAbilityTable", "profile": "LifeStaff"},
}
# The procedural items that the client rules read (the others are sounds, particles, and camera effects).
ITEM_TYPES = ("CAGE-TransitionInstant", "CAGE-TransitionBuffered", "CAGE-RangedAttack", "CAGE-CastSpell", "CAGE-Named",
              "CAGE-PayManaCost", "CAGE-Damage", "CAGE-ChannelSpell")
# The parameters of the items that the client rules read (the AI parameters of the casts are for creatures).
ITEM_PARAMS = ("Name", "Condition", "Input", "BackTicks", "NewAction", "NewFragment", "StartTime", "HoldTimeRequired",
               "RangedAttackName", "DamageTableRow", "ChargeValue", "ConsumeAmmo", "SpellName", "CostID", "DamageKey")
# The activities of an action that the client rules read.
ACTIVITY_TYPES = ("SetMannequinTag", "ClearActiveAbilityMoveInput", "CastSpell", "PayManaCost", "SetCooldownTimer", "PerformActiveAbility", "ForceWalk", "ForceRun",
                  "CastSpellTargeting", "CastSpellRaycast", "ChannelSpell", "ForceCameraLock", "CameraRelativeFacing", "DisableAutorun")
# The variant tags of the ranged fragments (the tags of the actions and of the marks).
VARIANT_TAGS = {"chargedaim", "penetratingaim", "heavyaim", "smokeaim", "hipaim", "aim", "aimdownsights", "shoot", "idle"}


def action_lists(names: list[str]) -> dict[str, ET.Element]:
    """@return Each action of the lists by name (the first list wins), and the named conditions: one name space for all action lists
    of the player (the engine loads them into one controller; the life staff reads Magic_Staff_Aim_Exit of the fire staff list), the
    lists of the tag set first."""
    actions, conditions, owners = {}, {}, {}
    others = sorted(n.rsplit("/", 1)[-1][:-len(".actionlist")] for n in source.paks().paths(CAGE) if n.endswith(".actionlist"))
    for name in names + [n for n in others if n not in names]:
        if name not in names:
            root = ET.fromstring(source.read(CAGE + name + ".actionlist").decode("utf-8", "replace"))
            for condition in root.find("Conditions") if root.find("Conditions") is not None else []:
                if condition.get("Name"):
                    conditions.setdefault(condition.get("Name"), condition)
            continue
        root = ET.fromstring(source.read(CAGE + name + ".actionlist").decode("utf-8", "replace"))
        for action in root.iter("Action"):
            actions.setdefault(action.get("Name"), action)
            owners.setdefault(action.get("Name"), name)
        for condition in root.find("Conditions") if root.find("Conditions") is not None else []:
            if condition.get("Name"):
                conditions.setdefault(condition.get("Name"), condition)
    return {"actions": actions, "conditions": conditions, "owners": owners}


def params_of(item: ET.Element) -> dict:
    params = item.find("ProceduralParams")
    return {child.tag: child.get("value") for child in params if child.tag in ITEM_PARAMS and child.get("value") not in (None, "")} \
        if params is not None else {}


def items_of(fragment: ET.Element) -> list[list]:
    """The timed procedural items of a fragment: [type, start, end, params]; an item lasts to the next item of its layer. The order
    of the data: the layers in their order, the items of a layer in their time order."""
    out = []
    for layer in fragment.findall("ProcLayer"):
        time = 0.0
        open_item = None
        for item in layer:
            if item.tag == "Blend":
                time += float(item.get("ExitTime", 0))
                continue
            if item.tag != "Procedural":
                continue
            if open_item is not None:
                open_item[2] = round(time, 5)
                out.append(open_item)
                open_item = None
            if item.get("type") in ITEM_TYPES:
                open_item = [item.get("type"), round(time, 5), None, params_of(item)]
        if open_item is not None:
            out.append(open_item)
    return out


def condition_json(element: ET.Element) -> list:
    """A condition tree: [type, attrs, children] (the form of movement.json)."""
    return [element.tag, {k: v for k, v in element.attrib.items() if k not in ("Name", "Comment")},
            [condition_json(child) for child in element]]


def guard_and(guard: list | None, condition: list) -> list:
    return condition if guard is None else ["And", {}, [guard, condition]]


def activities_of(action: ET.Element) -> list[list]:
    """The activities of an action that the client rules read: [type, attrs of Default (or of the activity), guard]. guard: the
    condition tree of the IfThenElse branches on the path (the Then branch: the IfCondition; the Else branch: its negation), or
    None at the top level (the enter and the exit of the action)."""
    out: list[list] = []

    def walk(node: ET.Element, guard: list | None) -> None:
        for child in node:
            if child.tag == "IfThenElse":
                condition_node = child.find("IfCondition")
                condition = ["And", {}, [condition_json(c) for c in condition_node]] if condition_node is not None else ["True", {}, []]
                then_node, else_node = child.find("ThenActivity"), child.find("ElseActivity")
                if then_node is not None:
                    walk(then_node, guard_and(guard, condition))
                if else_node is not None:
                    walk(else_node, guard_and(guard, ["Not", {}, [condition]]))
            elif child.tag == "Group":
                walk(child, guard)
            elif child.tag in ACTIVITY_TYPES:
                default = child.find("Default")
                out.append([child.tag, dict(default.attrib) if default is not None else dict(child.attrib), guard])

    acts = action.find("Activities")
    if acts is not None:
        walk(acts, None)
    return out


def action_json(action: ET.Element) -> dict:
    activation, deactivation = action.find("ActivationCondition"), action.find("DeactivationCondition")
    return {"fragment": action.get("FragmentName") or "",
            "activation": ["And", {}, [condition_json(c) for c in activation]] if activation is not None else None,
            "deactivation": ["And", {}, [condition_json(c) for c in deactivation]] if deactivation is not None and len(deactivation) else None,
            "activities": activities_of(action)}


def names_in(tree: list | None, out: set[str]) -> None:
    """Adds the names of the named conditions that a tree reads (IfExternalCondition ConditionName)."""
    if tree is None:
        return
    if tree[0] == "IfExternalCondition" and tree[1].get("ConditionName"):
        out.add(tree[1]["ConditionName"])
    for child in tree[2]:
        names_in(child, out)


def ability_of(action: ET.Element) -> tuple[str, bool, str]:
    """@return (the ability name of IfActiveAbilityMoveName in the activation, True when it needs the self-target key, the CostID of
    its IfManaCost), or ("", False, "")."""
    activation = action.find("ActivationCondition")
    if activation is None:
        return "", False, ""
    move = next(activation.iter("IfActiveAbilityMoveName"), None)
    if move is None:
        return "", False, ""
    negated = {id(node) for parent in activation.iter("Not") for node in parent.iter()}
    self_key = any(node.get("InputName") == "self_target" and id(node) not in negated for node in activation.iter("IfInput"))
    cost = next((node.get("CostID") for node in activation.iter("IfManaCost") if id(node) not in negated), "") or ""
    return move.get("AbilityName", ""), self_key, cost


def weapon(key: str, tags: set[str], record, find_fragment, tag_set, adb_chain, costs: dict, cooldowns: dict) -> dict:
    """The ranged entry of one tag set (see the module text)."""
    spec = RANGED_SETS[key]
    lists = action_lists(spec["lists"])
    actions_xml = lists["actions"]
    abilities: dict[str, dict] = {}
    for name, action in actions_xml.items():
        ability, self_key, cost = ability_of(action)
        if not ability:
            continue
        entry = abilities.setdefault(ability, {"id": ability, "crc": zlib.crc32(ability.lower().encode()), "actions": [],
                                               "costId": "", "cooldown": None, "minCooldown": None})
        entry["actions"].append({"action": name, "self": self_key})
        if cost and not entry["costId"]:
            entry["costId"] = cost
    names_by_case = {child.tag.lower(): child.tag for adb in adb_chain(tags) for child in (player.adb_root(adb).find("FragmentList") or [])}
    # The crawl: the start actions and the start actions of the abilities, then each action and fragment that a transition names.
    # The actions of the own lists of the weapon: also the actions that start by their activation alone (the cast of an aimed ability
    # after its raycast: ActionConditionSpellSpawnLocationState).
    own = [name for name, owner in lists["owners"].items() if owner in spec["own"]]
    pending = list(spec["start"]) + [a["action"] for ability in abilities.values() for a in ability["actions"]] + own
    actions: dict[str, dict] = {}
    fragments: dict[str, list] = {}
    queue: list[str] = []
    while pending or queue:
        if pending:
            name = pending.pop()
            if name in actions or name not in actions_xml:
                continue
            actions[name] = action_json(actions_xml[name])
            if actions[name]["fragment"]:
                queue.append(actions[name]["fragment"])
            continue
        name = queue.pop()
        if name in fragments:
            continue
        variants = []
        # Mannequin compares the fragment names with no case ("Attack_Start_Charge_Upperbody" of the action list is the fragment
        # Attack_Start_Charge_UpperBody of the databases).
        actual = names_by_case.get(name.lower(), name)
        for extra in [set()] + [{tag} for tag in sorted(VARIANT_TAGS)]:
            found = find_fragment(actual, tags, extra, False)
            if found is None:
                continue
            adb, element = found
            variant_tags = sorted(tag_set(element.get("Tags") or "") - tags)
            if any(v["tags"] == variant_tags for v in variants):
                continue
            items = items_of(element)
            variants.append({"tags": variant_tags, "fragment": record(adb, element, actual), "items": items})
            for item in items:
                params = item[3]
                if item[0] in ("CAGE-TransitionInstant", "CAGE-TransitionBuffered"):
                    if params.get("NewFragment"):
                        queue.append(params["NewFragment"])
                    if params.get("NewAction"):
                        pending.append(params["NewAction"])
        fragments[name] = variants
    for ability in abilities.values():
        row = cooldowns.get(ability["id"].lower())
        if row:
            ability["cooldown"], ability["minCooldown"] = row[1], row[2]
        ability["manaCost"] = costs.get(ability["costId"].lower(), 0.0) if ability["costId"] else 0.0
    # The named conditions: the conditions of the transitions and of the items, and each name that a tree reads, to the closure.
    names = {item[3].get("Condition") for variants in fragments.values() for v in variants for item in v["items"] if item[3].get("Condition")}
    for action in actions.values():
        names_in(action["activation"], names)
        names_in(action["deactivation"], names)
        for activity in action["activities"]:
            names_in(activity[2], names)
    conditions: dict[str, list] = {}
    missing: list[str] = []
    while names:
        name = names.pop()
        if name in conditions or name in missing:
            continue
        if name not in lists["conditions"]:
            missing.append(name)
            continue
        # A named condition is a condition node with a Name (<IfInput Name="Shoot_NoCharge_Bow" InputName="aim_shoot_release"/>).
        tree = condition_json(lists["conditions"][name])
        conditions[name] = tree
        names_in(tree, names)
    return {"actions": actions, "fragments": fragments, "abilities": sorted(abilities.values(), key=lambda a: a["id"]),
            "conditions": conditions, "start": spec["start"], "table": spec["table"], "profile": spec["profile"],
            "order": [name for name in actions_xml if name in actions],
            "missingConditions": sorted(missing)}


def spells() -> dict:
    """@return The spells of the spell tables: lower-case SpellID -> the values of the client rules (the cast distance and height, the
    area radius and height, the RangedAttackName and its CRC-32, the casting types, the duration, the ManaCost id, the prefab)."""
    out = {}
    for path in sorted(p for p in source.paks().entries if p.startswith(datasheet.TABLES_FOLDER) and p.endswith(".datasheet")
                       and "spelltable" in p):
        try:
            sheet = datasheet.Sheet(source.read(path))
        except Exception:
            continue
        if sheet.type != "SpellData":
            continue
        for row in sheet.rows():
            spell = str(row.get("SpellID") or "")
            if not spell or spell.lower() in out:
                continue
            attack = str(row.get("RangedAttackName") or "")

            def number(column: str) -> float | None:
                value = row.get(column)
                try:
                    return float(value) if value not in (None, "") else None
                except (TypeError, ValueError):
                    return None
            out[spell.lower()] = {"id": spell, "types": str(row.get("CastingTypes") or ""), "attack": attack,
                                  "attackCrc": zlib.crc32(attack.lower().encode()) if attack else 0,
                                  "castDistance": number("CastDistance"), "castHeight": number("CastHeight"),
                                  "radius": number("Radius"), "height": number("Height"), "duration": number("Duration"),
                                  "cameraLock": str(row.get("UseCameraTargetLock") or "").lower() in ("true", "1"),
                                  "manaCost": str(row.get("ManaCost") or ""), "prefab": str(row.get("SpellPrefabPath") or "")}
    return out


# The ammunition of each AmmoType when the slot is empty (the setting javelin.enable-ammoless-weapons; protocol notes 4.27).
AMMO_FALLBACK = {"Arrow": "DefaultArrow", "Shot": "DefaultShot"}


def flights(gamedata: dict) -> dict:
    """The shot rules of the profiles of the ranged and magic weapons with their flight (projectile-data.json of the backend copy,
    format 3: the columns of gamedata/projectile_data.cpp read_attack, read_slice, read_shot, read_ammo, read_weapon): for each
    rule its profile, attack, row, charge, spell, heavy flag, and the flight of the own projectile picture (the speed and the
    acceleration of the slice, the acceleration delay, the range, and the radius of the attack, the flying life of the slice)."""
    attacks = {(row[0], row[1]): row for row in gamedata["attacks"]}
    slices = gamedata["slices"]
    ammo = {row[0]: row for row in gamedata["ammo"]}
    weapons = {row[0]: {"profile": row[1], "ammoType": row[2], "ammo": AMMO_FALLBACK.get(row[2], "")} for row in gamedata["weapons"]}
    profiles = {w["profile"] for w in weapons.values()}
    rules = []
    for row in gamedata["shots"]:
        profile, attack_name = row[0], row[1]
        if profile not in profiles or profile.startswith("AI_"):
            continue
        attack = attacks.get((profile, attack_name))
        slice_index = row[4]
        if slice_index < 0:
            fallback = next((w["ammo"] for w in weapons.values() if w["profile"] == profile and w["ammo"]), "")
            slice_index = ammo[fallback][3] if fallback in ammo else -1
        flight = None
        if attack is not None and 0 <= slice_index < len(slices):
            s = slices[slice_index]
            flight = {"slice": s[0], "speed": s[1], "accel": s[4], "life": s[5], "accelDelay": attack[7], "maxRange": attack[3],
                      "radius": attack[4], "nearRange": attack[5]}
        rules.append({"profile": profile, "attack": attack_name, "attackCrc": zlib.crc32(attack_name.lower().encode()), "row": row[2],
                      "rowCrc": zlib.crc32(row[2].lower().encode()) if row[2] else 0, "charge": row[3], "spell": row[5],
                      "heavy": row[7], "ability": len(row) > 10 and row[10], "flight": flight})
    return {"weapons": {name: w for name, w in weapons.items() if not w["profile"].startswith("AI_")}, "shots": rules}


def spells_of(entries: list[dict]) -> set[str]:
    """@return The lower-case spell names that the ranged entries cast or target (CastSpell, CastSpellTargeting, CastSpellRaycast,
    ChannelSpell activities, CAGE-CastSpell items)."""
    out: set[str] = set()
    for entry in entries:
        for action in entry["actions"].values():
            for kind, attrs, _guard in action["activities"]:
                for key in ("SpellName", "SpellID", "Spell"):
                    if attrs.get(key):
                        out.add(attrs[key].lower())
        for variants in entry["fragments"].values():
            for variant in variants:
                for item in variant["items"]:
                    if item[0] in ("CAGE-CastSpell", "CAGE-ChannelSpell"):
                        for key in ("SpellName", "SpellID", "Spell"):
                            if item[3].get(key):
                                out.add(item[3][key].lower())
    return out


def finish(entries: list[dict], fragments: dict, clips: dict) -> None:
    """Adds the length and the loop flag of each ranged variant: the end of the first animation layer of its record (the sum of
    the ExitTime values, then the clip duration over its speed; a loop clip: loop). 0: not known."""
    for entry in entries:
        for variants in entry["fragments"].values():
            for variant in variants:
                layers = fragments.get(variant["fragment"], {}).get("layers") or [[]]
                time, length, loop = 0.0, 0.0, False
                for item in layers[0] if layers else []:
                    time += item.get("exit", 0.0)
                    clip = clips.get(item.get("clip") or "") or {}
                    duration = clip.get("duration") or 0.0
                    if duration and item.get("speed"):
                        length = time + (duration - item.get("start", 0.0)) / item["speed"]
                    loop = bool(item.get("loop"))
                variant["length"] = round(length, 5)
                variant["loop"] = loop
